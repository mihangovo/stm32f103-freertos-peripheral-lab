# 调试复盘：Storage 历史记录一直不更新，根因是 FreeRTOS 堆不够、任务创建静默失败

日期：2026-07-12

## 现象

- Storage 历史记录页面固定显示很早以前的 15 条旧消息，之后不管发多少条新 UART 消息，都不会覆盖/更新。
- Setting 页面里"清除所有历史记录"点了也没反应，显示的还是那些旧数据。
- UART 监视页面、OLED、姿态显示等其它功能全部正常，只有 Storage 这一块不工作。

## 排查过程

1. 先怀疑过 `Meta_Load()` 没被调用（`storage_task.c` 里 `Storage_Task_Entry` 开头有一行被注释掉的 `// Meta_Load();`）——排查后发现这是误报：真正生效的 `Meta_Load()` 调用在 `main.c` 里，在调度器启动前手动调用了一次，物理上能正常读到 flash 数据（开机日志 `load magic=5a5a0001 led=1` 能证明）。
2. 在 `Storage_RequestSaveHistory`/`History_Append`/`Storage_ReadHistoryByOffset` 里加了 printf 诊断，发现发送新消息后队列里 `[StorageReq] SAVE_HISTORY queued ok` 会打印（入队成功），但**处理这条命令的 `[HistApp] enter...` 永远不出现**——说明命令进了队列，但没有任何东西在消费它。
3. 关键一步：在 `Storage_Task_Entry` 函数最开头加了一行 `printf("[StorageTask] alive...")`，这行也**从来不出现**——而这是这个任务函数的第一行代码，只要任务线程真的启动了就该立刻打印。这说明不是"任务卡住了"，而是**这个任务从始至终没有运行起来**。
4. 顺着这个线索去查 `Core/Src/freertos.c` 里 `MX_FREERTOS_Init()`：`StorageTaskHandle = osThreadNew(Storage_Task, NULL, &StorageTask_attributes);` 这行**从来没有检查返回值**。在 `/* USER CODE BEGIN RTOS_THREADS */` 区域加了 `xPortGetFreeHeapSize()` 和打印所有任务句柄的诊断，直接实锤：`Storage=0`（NULL），`free heap after all creation = 640 bytes`（几乎耗尽）。

## 根因

`FreeRTOSConfig.h` 里 `configTOTAL_HEAP_SIZE` 只配置了 **10240 字节**（10KB），这是 FreeRTOS 自己管理任务栈/队列/互斥锁的动态内存池，跟单片机总 RAM（64KB）是两回事。项目迭代过程中：
- 为了修复"UART/Storage 命令队列满导致静默丢消息"这个更早的 bug，把 `StorageCmdQueue` 深度调到 10、新增的 `UartLineQueue` 深度调到 16；
- 加上原本就有的 6 个任务（KeyScan/UIManager/MPURead/Heartbeat/Storage/Led）的栈、4 个互斥锁；

这些加在一起已经逼近甚至超过 10240 字节的预算。`Storage` 任务恰好是**创建顺序里排在比较靠后、需要的栈（512×4=2048字节）又不小**的一个，轮到它申请内存时堆已经不够，`osThreadNew()` 返回 `NULL`（创建失败）。因为 CubeMX 生成的模板代码从不检查 `osThreadNew`/`osMutexNew`/`osMessageQueueNew` 的返回值，所以整个失败过程**没有任何报错、没有崩溃、没有卡死**，只是这一个任务从开机起就不存在，处理它命令的逻辑自然也就"什么都不做"。

## 经验教训

- **动态内存分配失败在 FreeRTOS 里默认是静默的**：`osThreadNew`/`osMutexNew`/`osMessageQueueNew` 等创建类API在堆不足时都会返回 `NULL`，但 CubeMX 生成的样板代码从不检查这个返回值。以后新增/调大任何任务栈、队列深度、互斥锁之后，**必须验证堆预算还够不够**，不能只看"编译通过、Flash/RAM静态占用百分比正常"就以为没问题——静态占用（.data/.bss段）和 FreeRTOS 堆的动态占用是两个完全不同的统计口径，链接器给出的 RAM 使用率不包含堆内部还剩多少。
- **调试"某个功能完全没反应、但也不报错、不崩溃"这类问题时**，如果怀疑是某个任务没跑起来，最快的验证方法就是在任务函数的**第一行**加一个无条件 printf——如果开机后这行都不出现，直接把怀疑范围锁定到"任务根本没被创建"，不用再纠结任务内部逻辑。
- 用 `xPortGetFreeHeapSize()`（创建完所有内核对象之后打印一次）和 `uxTaskGetStackHighWaterMark(任务句柄)`（运行一段时间、各任务都跑到过最坏情况之后打印，单位是"字"，乘4转字节）可以分别看到"总剩余多少"和"每个任务实际用了多少、还剩多少余量"，用来把 `configTOTAL_HEAP_SIZE` 和各任务的 `stack_size` 设置到"够用又不浪费"的合理值。

## 修复

在 STM32CubeMX 里把 `configTOTAL_HEAP_SIZE` 从 10240 调大到 20480（用户已验证生效，Storage 任务恢复正常）。后续可以用上面提到的高水位线诊断，把各任务栈往回收一收、把总堆大小收敛到一个更精确的数值，而不是简单粗暴地翻倍。

## 附：加诊断本身又引出一个新坑——`printf` 把小栈任务撑爆导致整机死锁

为了拿到上面说的"各任务栈高水位线"数据，在 `HeartbeatTask`（本来只负责闪一个LED，`stack_size` 只给了 `128*4=512` 字节）里加了一行每10秒打印一次、带6个 `%lu` 参数的 `printf`。加上之后复现出一个新问题：**姿态显示页面停留约10秒后，整机彻底卡死**——不只是姿态页面不刷新，连跟这个页面完全不相关的心跳灯也停止闪烁、按键也没反应，说明不是某个任务卡住，而是**整个调度器都停了**。

一开始怀疑过是 MPU6050 的软件模拟 I2C 卡总线（开机日志里确实有 `dmp_init err` 连续失败十几次的现象，一度以为是硬件接触不良），但复位、断电重新上电都稳定复现在"约10秒"这个时间点——太规律，不像随机的硬件接触问题，转而怀疑是当天新加的代码。

**根因**：新加的 `printf` 调用参数多、格式字符串长，`newlib` 的 `printf` 实现本身需要用掉不少栈空间做参数处理和数字转字符串，而 `HeartbeatTask` 的栈只有 512 字节这么小（正常情况下只是切换GPIO电平+`osDelay`，几乎不需要栈），第一次触发这行 printf（计时器计满10次、约10秒后）就把这个任务的栈撑爆。工程里本来就开着 `configCHECK_FOR_STACK_OVERFLOW` 检测，触发后 `vApplicationStackOverflowHook` 会打印提示然后 `while(1)` 死循环——这个死循环直接冻结了整个系统的调度。

**修复**：把 `HeartbeatTask` 的 `stack_size` 从 `128*4` 调到 `320*4`，验证不再卡死。**这个改动是直接手改 `freertos.c` 里的数值（不在 USER CODE 区域内，是 CubeMX 生成的部分），下次通过 CubeMX 重新生成工程会被覆盖，需要记得同步去 CubeMX 里把这个任务的栈也调大，才能长期保留。**

**经验教训**：
- **调试时"随手加一行 printf"不是零成本的**，尤其是加进那些"本来任务逻辑很简单、给的栈故意留得很小"的任务里——`printf` 参数越多、格式越复杂，需要的临时栈空间越大，很容易把小栈任务直接撑爆。
- **诊断代码本身也可能引入新 bug，加完之后同样要按系统性排障的方式验证**（这次能快速定位，是因为"卡死时间点"和"新加的10秒周期打印"这个时间上的巧合被注意到了，而不是一上来就怀疑硬件）。
- "全系统彻底冻结（连不相关的任务、心跳灯都停）"是一个重要信号，指向"调度器本身停了"（栈溢出损坏了调度器数据、或溢出处理钩子里的死循环），而不是"某个任务的业务逻辑卡住"（那种情况下不相关的任务应该还能正常跑）——排查这类"局部现象 + 全局停摆"同时出现的问题时，要往"全局共享的东西被破坏"这个方向想。
