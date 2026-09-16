# Battery Guardian

纯 C++ / Win32 API 托盘程序，无第三方框架或依赖库。

- 启动后显示系统默认应用程序托盘图标和调试控制台，不显示主窗口。
- 通过 Windows 自带 WMI 的 `Win32_ProcessStartTrace` 事件监听新进程，每行输出一个 exe 文件名（不包含完整路径）。监听就绪前已启动的进程不会补报。
- 监听运行在独立线程；退出时停止监听并释放 WMI、线程和控制台资源。
- 启动时读取 exe 同目录下的 `blacklist.txt`，命中的新进程通过 `SetProcessAffinityMask` 绑定到 E 核。
- 右键图标显示“Exit”菜单，点击后删除图标并退出程序。
- 托盘菜单的 `Hide Terminal` / `Show Terminal` 可隐藏或显示调试控制台；最小化时显示 `Show Terminal`，点击可恢复窗口。隐藏后继续监听和输出日志，再次显示时保留控制台缓冲区内容。启动时仍默认显示控制台。
- Windows 资源管理器重启后自动重新添加托盘图标。

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

控制台显示 `Listening for process starts.` 后，启动其他程序即可查看输出。若显示 `Access denied`，请以管理员身份运行；其他监听错误也会输出 HRESULT。直接关闭控制台窗口会结束整个程序，建议使用托盘菜单的 `Exit` 正常退出。

显示/隐藏功能使用 `GetConsoleWindow` 和 `ShowWindow`，适用于 Windows Console Host 的独立控制台窗口。如果默认终端由 Windows Terminal 托管，请在系统终端设置中选择 Windows Console Host；Windows Terminal 的伪控制台句柄不对应实际显示的终端窗口，无法用此方式控制其显示/隐藏。参见 [GetConsoleWindow 文档](https://learn.microsoft.com/en-us/windows/console/getconsolewindow)。

## 黑名单与 E 核绑定

编辑运行程序旁边的 `blacklist.txt`（使用 `build.cmd` 时是 `build\blacklist.txt`）。文件使用 UTF-8 编码，可带 BOM，每行填写一个 exe 文件名，例如：

```text
# Background applications
example.exe
another-app.exe
```

匹配不区分大小写，忽略行首行尾空白、空行和 `#` 注释行。不支持完整路径、通配符或行尾注释。修改后重启 Battery Guardian 生效。构建时只在目标文件不存在时复制模板，保留已编辑的黑名单；模板默认没有启用的条目。文件缺失或格式错误时停止应用绑定规则，但继续监听和打印进程。

程序使用 `GetLogicalProcessorInformationEx` 的 `EfficiencyClass` 区分核类型，将最低级别的所有逻辑处理器作为 E 核（多级架构只选择最低级别）。只有所有核心处于同一处理器组、运行 64 位版本且存在至少两种级别时才设置亲和性。没有 E 核、Windows 未报告核类型或多个处理器组的机器会明确提示并跳过绑定。

命中后会核对 PID 对应的实际 exe 文件名和创建时间，再设置亲和性。成功输出文件名、PID 和掩码；进程已退出、权限不足或设置失败会输出原因。启动事件存在延迟，无法保证进程从第一条指令起就运行在 E 核上。规则只对监听就绪后的启动事件生效；退出本程序不会还原已设置的亲和性，目标程序也可能自行修改亲和性。Windows 的进程亲和性可被子进程继承。

核类型与亲和性行为参考 Microsoft 文档：[PROCESSOR_RELATIONSHIP](https://learn.microsoft.com/en-us/windows/win32/api/winnt/ns-winnt-processor_relationship)、[SetProcessAffinityMask](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-setprocessaffinitymask)。

## 测试

```bat
cmake -S . -B out -A x64 -DBATTERY_GUARDIAN_BUILD_TESTS=ON
cmake --build out --config Release
ctest --test-dir out -C Release --output-on-failure
```

测试覆盖黑名单解析、核类型选择以及对测试自身创建的进程设置和回读亲和性；没有可识别 E 核的机器会跳过实际 E 核绑定检查。完整 WMI 事件链需要足够权限。
