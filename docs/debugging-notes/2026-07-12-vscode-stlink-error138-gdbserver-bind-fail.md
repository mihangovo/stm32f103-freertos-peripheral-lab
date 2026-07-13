# 调试复盘：VSCode 烧录/调试报 "could not connect (error 138)"，本质是 ST-LINK GDB server 绑端口失败

日期：2026-07-12

## 现象

在 VSCode 里用 STM32CubeIDE for VS Code 插件点"调试/烧录"，控制台先输出一段乱码：

```
Failed to decode cstring 'could not connect (error 138): ...'. {}
```

这段乱码本身**跟真实故障无关**，是 Windows 通用错误码 138 的文本映射，不能按字面理解。往上翻能看到真正的原因，在 `ST-LINK_gdbserver.exe`（版本 7.13.0）自己的启动日志里：

```
Failed to bind to port 61235, error code -1: No error
Failure starting SWV server on TCP port: 61235
Failed to bind to port 61234, error code -1: No error
Failure starting GDB server: TCP port 61234 not available.
Shutting down...
Exit.
```

即 gdbserver 启动时绑定它自己的 GDB/SWV 监听端口（默认 61234/61235）失败，进程直接退出；随后 VSCode 里的 gdb 客户端因为连不上一个根本没启动起来的 server，才报出那段乱码。

## 已排除的原因（按排查顺序）

1. **不是 ST-Link 硬件/驱动坏了**：全程用 Keil 和 `STM32_Programmer_CLI.exe`（`C:\Users\arch\AppData\Local\stm32cube\bundles\programmer\<版本>\bin\`，这个 exe 就是插件自带工具链的一部分，不用单独装）都能正常连接/烧录，说明 SWD 物理链路、USB 驱动本身没问题。
2. **不是残留进程占端口**：杀掉遗留的 `stlinkserver.exe`（旧版 ST-Link Server，2.1.1+st.8）无效；`netstat`/`Get-NetTCPConnection` 确认失败当下没有任何进程在监听 61234/61235。
3. **不是端口号本身的问题**：在 `.vscode/launch.json` 里把 `serverPort` 从 61234 改成 61334（`stlinkgdbtarget` 配置支持这个字段，SWV 端口固定是 `serverPort+1`），换了端口一样失败——说明卡住的不是"这两个端口"，而是 `ST-LINK_gdbserver.exe` 这个程序本身，不管用哪个端口都绑不上。
4. **不是权限层级问题**：以管理员身份运行 VSCode 后重试，报错完全一样——说明拦截不是靠用户权限能绕过的那种（排除了"只是没点击防火墙允许弹窗"这类简单情况）。
5. **不是 Windows 的端口排除区间**（Hyper-V/WSL/Docker 常见诱因）：`netsh interface ipv4/ipv6 show excludedportrange protocol=tcp` 两次查都是空的。
6. **不是防火墙规则冲突**：`Get-NetFirewallRule` 查到的两条 `st-link_gdbserver.exe` 规则都是 `Allow`+`Profile=Public`（当前网络分类正好是 Public），没查到冲突的 Block 规则（受限于非管理员权限，没能查更细的按端口过滤的规则）。
7. **不是文件被杀软隔离/篡改**：`Get-AuthenticodeSignature` 验证 `ST-LINK_gdbserver.exe` 签名有效，是 STMicroelectronics 官方签名；Defender 操作日志里没有对应的检测/拦截事件（但这类日志本身查得不够全，不能 100%排除 Defender 网络防护）。
8. **不是 Clash 代理/TUN 模式**：用户确认完全退出 Clash 后重试，一样失败。

## 目前观察到的（未 100% confirm 因果关系的）关联现象

用户提到当时开着手机热点（连接一个固定 IP 的 ESP32），关掉热点后，同一个 VSCode 配置（`serverPort=61334`）**立刻烧录+调试成功**（完整走完了 `Debugger connected` → 固件下载 → 命中 `main()` 断点的全流程）。

**关键澄清（用户确认）**：在关热点这次之前，用户已经反复重启过 VSCode 很多次（包括以管理员身份运行）来尝试修复，全部无效——所以"重启 VSCode"本身不是有效的修复手段，这条路已经被现实验证排除掉了。**关闭热点是唯一一次让状态从"坏"变"好"的操作。**

之后重新打开热点、连上 ESP32、重启 VSCode 再试，依然烧录成功——但这**不代表热点不是原因**，更合理的解释是：**热点这一头"从关到开"的那一次连接过程（尤其是 ESP32 用固定 IP 接入）可能才是真正触发问题的时机**，而不是"热点保持开启"这个状态本身持续在捣乱。问题被关热点这个动作修复之后，后续再开热点、因为没有重新触发那个异常的连接时序，所以没有再复现。这跟"重启电脑后第一次能用，后面用着用着又不行"的历史现象是同一类模式：**某个瞬时的网络事件（热点连接瞬间 / 开机瞬间）会让 Windows 网络栈的某个状态被搞坏，直到下一次同类事件或重启才会被刷新/修复**，具体机制没有拿到实锤证据（受限于没有管理员权限查 WFP 过滤规则）。

（插曲：中途还遇到过一次报错 `ST-Link enumeration failed / ST-LINK DLL error`，看起来很像，但那次单纯是 ST-Link 排线没插好，跟这个坑无关，插好就好了——排查这类问题时要注意先看清楚 gdbserver 自己日志里具体卡在哪一步，`Failed to bind to port` 和 `enumeration failed` 是两种完全不同的故障点，报错最后那句乱码的 `could not connect (error 138)` 只是 gdb 客户端在 server 启动失败后的通用提示，没有区分度，不能只看这一行。）

## 当前结论与应对方式

**根因最终没有 100% 定位到**，但排查过程本身值得记录：通过 `netstat`/`Get-NetTCPConnection`（有没有进程占端口）、改 `launch.json` 的 `serverPort`（是不是端口号问题）、**反复重启 VSCode、以及以管理员身份重启 VSCode（用户确认试了很多次，均无效）**、`netsh show excludedportrange`（是不是 Hyper-V/WSL 端口保留）、`Get-NetFirewallRule`（是不是防火墙规则）、`Get-AuthenticodeSignature`（文件是否被篡改/杀软隔离）、Windows Defender 操作日志（有没有拦截记录）等一系列命令行取证，逐一排除了"进程残留""端口号""VSCode本身重启/权限不够""端口保留区""防火墙规则""文件损坏""Clash代理"这些看似合理的假设。**唯一实测有效的修复动作是关闭手机热点**；没能再往下挖到 Windows 网络栈里具体是什么状态被改热点连接过程弄坏的（需要管理员权限查 WFP 过滤规则，这次条件不够）。

**遇到这个问题时的应对顺序（按验证有效性排列，重启 VSCode 本身单独用没用）**：
1. 如果有手机热点/其它临时网络连接（尤其是给单片机/ESP32这类设备开的、可能带固定IP的热点）正开着，先关掉，完全退出 VSCode 再重新打开试一次。
2. 如果没用，试着切换一下其它网络连接状态（断开重连WiFi，或禁用再启用网卡）。
3. 如果还没用，重启电脑通常能临时解决（历史上验证有效多次，但机制不明，不保证每次都行）。
4. 如果暂时没空折腾，可以绕开 VSCode 的调试连接，直接用 `STM32_Programmer_CLI.exe` 命令行烧录（不经过 gdbserver 这层，全程验证有效，插件自带不用单装）：
   ```
   STM32_Programmer_CLI.exe -c port=SWD -w "<elf或hex路径>" -v -rst
   ```

如果以后再遇到、还想彻底挖根因，建议从这次没来得及做的地方继续：
- 用管理员权限的 PowerShell 查 `Get-NetFirewallPortFilter`/WFP 过滤规则（这次因为终端不是管理员权限，一直卡在这一步）。
- 查 Defender 的 `Get-MpPreference` 里 `EnableNetworkProtection` 到底是不是开着的。
- 下次触发热点连接（尤其是 ESP32 固定IP接入）的瞬间，同时抓一次 `netsh interface ipv4 show excludedportrange` 和网络适配器事件日志，看那个时间点 Windows 网络栈到底发生了什么变化。
