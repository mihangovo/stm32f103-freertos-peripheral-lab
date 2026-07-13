# 看门狗（IWDG + 多任务存活性聚合）设计

## 背景与动机

2026-07-12 晚上发生过一次真实故障：`HeartbeatTask` 为了打印栈高水位线诊断信息，把自己本就很小的栈（128 word）撑爆，触发 `vApplicationStackOverflowHook` 死循环，导致整个调度器瘫痪（心跳灯、按键、姿态显示全部冻结），只能靠人工拔插电源恢复。这类故障目前完全没有自动恢复手段。

看门狗能把这类"任务卡死/调度器瘫痪"的真实故障，从"人工发现+手动复位"变成"硬件自动复位"，是当前评估下来性价比和故事完整性都最高的加固项。

## 目标

- 任意一个被监控任务卡死（无论是忙等死循环、还是等一个永远不会被释放的资源），系统能在几秒内自动复位恢复。
- 不误报：任务的正常空闲等待（比如 UI 页面等按键、Storage 等命令）不能被误判为"卡死"。
- 复位后能从日志上区分"是看门狗救回来的"还是"正常上电"，方便事后统计真实故障频率。

## 非目标

- 不做"卡死原因自动诊断"（比如记录是哪个任务卡死、调用栈是什么），只做"检测到卡死就复位"。
- 不做复位信息的 flash 持久化和 UI 展示，本轮只在开机时 printf 一次。用户明确选择先不做，后续想加可以单独立项。
- 不处理 main.c 里调度器启动前的外设初始化阶段（OLED/MPU/DMP/NORFLASH init）卡死的情况——这部分依赖另一个还未启动的 InitTask 重构项（见项目 backlog），本轮看门狗只覆盖调度器启动之后的运行阶段。

## 架构

新增一个独立的、职责单一的 `WatchdogTask`：

- 每 500ms 醒来一次，遍历所有被监控任务的"最近一次打卡时间戳"，跟当前 tick 比较。
- 如果全部任务的时间戳都在 1500ms 以内（新鲜），调用 `HAL_IWDG_Refresh(&hiwdg)` 喂狗。
- 只要有一个任务的时间戳超过 1500ms 没更新，就跳过喂狗、什么都不做（可以 printf 一条诊断日志说明是哪个任务过期），任由 IWDG 在约 3 秒后自然超时、硬件复位。

`WatchdogTask` 本身刻意写得极简（只做"读时间戳数组 + 比较 + 喂狗"），不做其它业务逻辑，因为它是整个安全机制的单点——越简单，它自己出 bug 的概率就越低。

### 为什么是"多任务聚合"而不是"心跳任务直接喂狗"

如果只让 `HeartbeatTask` 自己喂狗，只能覆盖"心跳任务自己挂了"这一种情况；如果其它任务（比如 UI、Storage）卡死但心跳还在正常打印，看门狗完全发现不了。多任务聚合式设计能覆盖"任意一个任务卡死"，跟这次真实故障（心跳任务自己挂掉）的教训也完全对得上，同时对未来别的任务出问题也有防护。

### 为什么 WatchdogTask 优先级选 osPriorityLow

工程里现有 6 个任务优先级从高到低大致是 Normal > BelowNormal > Low（Heartbeat 是目前唯一的 Low）。把 WatchdogTask 也放在 Low：

- 保证它在正常情况下能稳定拿到 CPU 完成检查（不需要抢占谁）。
- 附带识别"CPU 被某个更高优先级任务死循环占满"这类故障：这种情况下 WatchdogTask 自己也会被饿死、没法喂狗，IWDG 一样会自然超时复位——不需要额外写检测代码就顺带覆盖了这类场景。

## 数据结构与打卡机制

```c
// watchdog_task.h
typedef enum {
    WDG_TASK_KEY = 0,
    WDG_TASK_UI,
    WDG_TASK_MPU,
    WDG_TASK_HEARTBEAT,
    WDG_TASK_STORAGE,
    WDG_TASK_LED,
    WDG_TASK_COUNT
} WatchdogTaskId_t;

void Watchdog_Checkin(WatchdogTaskId_t id);
void Watchdog_Task_Entry(void *argument);
```

```c
// watchdog_task.c
static volatile uint32_t g_task_alive_tick[WDG_TASK_COUNT];

void Watchdog_Checkin(WatchdogTaskId_t id)
{
    g_task_alive_tick[id] = HAL_GetTick();
}
```

每个字段只有对应的那个任务会写，`WatchdogTask` 只读，不存在多写者竞争，不需要互斥锁（Cortex-M 上对齐的 32 位读写本身是原子的）。

## 需要修改的现有任务

| 任务 | 现状 | 改动 |
|---|---|---|
| Key_Scan_Task | `osDelay(10)` 固定周期循环 | 循环里加一行 `Watchdog_Checkin(WDG_TASK_KEY)` |
| MPU_Read_Task | `osDelay(g_mpu_read_period)`（默认200ms）固定周期循环 | 加一行打卡 |
| Hearbeat_Task | `osDelay(500)*2` 固定周期循环 | 加一行打卡 |
| Led_Task | `osDelay(500)` 固定周期循环 | 加一行打卡 |
| Storage_Task_Entry | 阻塞在 `osMessageQueueGet(..., osWaitForever)`，空闲时不会主动醒来 | 改成 `osMessageQueueGet(..., 1000)`（1000ms超时），超时（无消息）就打卡后继续循环，不影响原有的命令处理逻辑 |
| UI_Manager_Task_Entry | 无 `on_tick` 的页面上也是 `osMessageQueueGet(KeyQueueHandle, ..., osWaitForever)` | 同样把 `osWaitForever` 改成 1000ms 超时，超时打卡后继续循环 |

Storage/UI 这两处是本设计里唯一需要改动既有业务逻辑（而不是纯新增一行）的地方，改动本身对功能无影响——只是把"无限等待"改成"有限等待+超时后空转一次"。

## IWDG 外设配置

已通过 CubeMX GUI 激活（用户操作，Timers → IWDG → Activated），参数：

- `Prescaler = IWDG_PRESCALER_64`
- `Reload = 1874`
- 对应 LSI 标称 40kHz 下约 3000ms 超时（LSI 本身有精度误差，实际会有一定浮动，量级仍在 3 秒左右，满足需求）。

`MX_IWDG_Init()` 已由 CubeMX 生成在 `Core/Src/iwdg.c`，在 `main()` 里 `MX_IWDG_Init()` 调用点是标准 CubeMX 外设初始化顺序的一部分（调度器启动前）。

## 复位原因诊断

在 `main.c` 的 `USER CODE BEGIN 2`（其它外设初始化附近，串口已可用之后）加：

```c
if (__HAL_RCC_GET_FLAG(RCC_FLAG_IWDGRST))
{
    printf("[Watchdog] last reset was caused by IWDG (task hang recovered)\r\n");
}
__HAL_RCC_CLEAR_RESET_FLAGS();
```

只做 printf 提示，不做 flash 持久化、不做 UI 展示（明确的范围收窄决定）。

## 时序与预期恢复延迟

- 任务打卡间隔：Key 10ms / MPU 200ms / Heartbeat 500ms / Led 500ms / Storage、UI 空闲时最长 1000ms。
- 存活阈值：1500ms（覆盖 Storage/UI 最长打卡间隔的 1.5 倍余量）。
- `WatchdogTask` 检查周期：500ms。
- IWDG 超时：约 3000ms。

worst-case 端到端恢复延迟（从任务卡死那一刻到硬件实际复位）大约 3.5~5 秒——这是"1500ms 判定过期" + "最多 500ms 才轮到下一次检查" + "IWDG 本身还要 3000ms 才超时"这几层安全余量叠加的正常结果，不需要进一步压缩。

## 已知的架构边界（本轮不覆盖）

- main.c 里调度器启动前的外设初始化阶段（OLED/MPU/DMP/NORFLASH）如果卡死，IWDG 此时尚未开始计数（`MX_IWDG_Init()` 在这些初始化之后才调用），看门狗覆盖不到。这个阶段的加固依赖尚未开始的 InitTask 重构项。
- 复位原因/次数不做持久化，多次复位后无法从设备本身查到历史统计，只能看当次开机日志。

## 测试计划

1. 编译烧录后正常运行观察：串口应该持续没有 `[Watchdog]` 过期日志，说明没有误报（尤其要盯着 Storage/UI 长时间空闲的场景，确认没被误判为卡死）。
2. 故意制造一次任务卡死（比如临时在某个任务里加一个 `while(1);` 死循环，不调用任何阻塞API），验证约 3.5~5 秒后设备自动复位，且复位后串口打印 `[Watchdog] last reset was caused by IWDG`。
3. 验证正常上电（非看门狗触发）不会打印这条诊断信息，确认复位标志读取和清除逻辑正确。
