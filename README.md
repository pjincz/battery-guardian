# Battery Guardian

纯 C++ / Win32 API 托盘程序，无第三方框架或依赖库。

- 启动后显示系统默认应用程序托盘图标和终端式日志窗口。
- 通过 Windows 自带 WMI 的 `Win32_ProcessStartTrace` 事件监听新进程，每行输出一个 exe 文件名（不包含完整路径）；启动和重载配置后还会扫描全部运行中的进程，为命中的进程应用规则。
- 监听运行在独立线程；退出时停止监听并释放 WMI、线程和日志窗口资源。
- 启动时读取 exe 同目录下的 `blacklist.txt`，命中的新进程通过 `SetProcessAffinityMask` 绑定到 E 核。
- 右键图标显示“Exit”菜单，点击后删除图标并退出程序。
- 托盘菜单的 `Hide Terminal` / `Show Terminal` 可隐藏或显示日志窗口；最小化时显示 `Show Terminal`，点击可恢复窗口。点击窗口的关闭按钮或按 Alt+F4 只隐藏窗口，程序继续运行。启动时默认显示窗口。
- 左键单击托盘图标直接切换日志窗口的显示/隐藏；右键菜单中该操作以默认项加粗显示。
- 右键菜单的 `Reload blacklist.txt` 重新读取名单，日志窗口显示加载结果；监听线程未运行时该选项不可用。
- Windows 资源管理器重启后自动重新添加托盘图标。

配置生命周期统一为 `LoadBlacklist`（读取并应用）和 `UnloadBlacklist`（停止匹配并恢复）。启动时先建立 WMI 订阅，再加载名单，只进行一次全量扫描；重载执行卸载再加载；正常退出通过卸载恢复设置。

## 编译和运行

安装 Visual Studio 或 Build Tools 的“使用 C++ 的桌面开发”工作负载（包含 Windows SDK），然后运行：

```bat
build.cmd
build\BatteryGuardian.exe
```

也可使用 CMake：

```bat
cmake -S . -B build
cmake --build build --config Release
```

使用 Visual Studio 生成器时，程序位于 `build\Release\BatteryGuardian.exe`。

Windows 可能将新图标放入任务栏的隐藏图标区域，点击托盘旁的箭头即可找到。

程序内嵌 `requireAdministrator` 权限清单，正常双击启动时 Windows 会请求 UAC 管理员授权（已提升权限的环境不重复提示）；取消授权则程序不会启动，无需手动选择“以管理员身份运行”。

日志窗口显示 `Listening for process starts.` 后，启动其他程序即可查看输出。若提升权限后仍显示 `Access denied`，需检查 WMI 权限或系统安全策略；其他监听错误也会输出 HRESULT。使用托盘菜单的 `Exit` 退出程序。

日志窗口使用纯 Win32 窗口与只读 EDIT 控件实现，可选择、复制和滚动查看文本，不提供命令行输入。隐藏期间持续接收日志，最多保留最近约 256K 个 UTF-16 字符，超出时淘汰旧内容。工作线程通过有界缓冲区异步发送日志给 UI，避免关闭或隐藏窗口影响进程监听。无需更改 Windows 默认终端设置。

这里不再使用 `AllocConsole`：原生控制台的关闭信号即便被处理也会结束进程，无法可靠实现关闭即隐藏。参见 [CTRL+CLOSE 文档](https://learn.microsoft.com/en-us/windows/console/ctrl-close-signal)。

## 黑名单与 E 核绑定

编辑运行程序旁边的 `blacklist.txt`（使用 `build.cmd` 时是 `build\blacklist.txt`）。文件使用 UTF-8 编码，可带 BOM，每行填写一个 exe 文件名，例如：

```text
# Background applications
example.exe
another-app.exe
```

匹配不区分大小写，忽略行首行尾空白、空行和 `#` 注释行。不支持完整路径、通配符或行尾注释。修改后点击托盘菜单的 `Reload blacklist.txt` 即可生效，也可重启程序。重新加载在监听线程中完成：先遍历本程序记录的进程并恢复原始亲和性，再读取配置、扫描全部运行进程并应用新名单。读取失败则保留旧名单并重新扫描应用；空文件会清空名单并恢复旧设置。启动时文件缺失或格式错误则不启用任何规则。

构建时只在目标文件不存在时复制模板，保留已编辑的黑名单；模板默认没有启用的条目。

程序使用 `GetLogicalProcessorInformationEx` 的 `EfficiencyClass` 区分核类型，将最低级别的所有逻辑处理器作为 E 核（多级架构只选择最低级别）。只有所有核心处于同一处理器组、运行 64 位版本且存在至少两种级别时才设置亲和性。没有 E 核、Windows 未报告核类型或多个处理器组的机器会明确提示并跳过绑定。

命中后会核对 PID 对应的实际 exe 文件名和创建时间，再设置亲和性。首次修改时保存原始掩码和进程句柄，重复事件或扫描不会覆盖记录。正常通过托盘 `Exit` 退出时停止监听，遍历记录并恢复仍存活进程的原始掩码（不简单重置为全部 CPU）。使用句柄恢复可避免 PID 复用造成误操作，未被本程序修改的进程不会被恢复逻辑改动。进程已退出则清理记录；设置或恢复失败会输出错误，重载时恢复失败的记录会保留以便下次重试。

启动事件存在延迟，无法保证进程从第一条指令起就运行在 E 核上。目标程序可能自行修改亲和性，恢复时会使用本程序首次记录的设置。Windows 的进程亲和性可被子进程继承，子进程记录的是本程序接管它时的设置。强制终止程序、崩溃或断电无法保证执行恢复。

核类型与亲和性行为参考 Microsoft 文档：[PROCESSOR_RELATIONSHIP](https://learn.microsoft.com/en-us/windows/win32/api/winnt/ns-winnt-processor_relationship)、[SetProcessAffinityMask](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-setprocessaffinitymask)。

## 测试

```bat
cmake -S . -B out -A x64 -DBATTERY_GUARDIAN_BUILD_TESTS=ON
cmake --build out --config Release
ctest --test-dir out -C Release --output-on-failure
```

测试覆盖黑名单解析、核类型选择以及对测试自身创建的进程设置和回读亲和性；没有可识别 E 核的机器会跳过实际 E 核绑定检查。完整 WMI 事件链需要足够权限。
