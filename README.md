# Battery Guardian

纯 C++ / Win32 API 托盘程序，无第三方框架或依赖库。

- 启动后显示系统默认应用程序托盘图标和调试控制台，不显示主窗口。
- 通过 Windows 自带 WMI 的 `Win32_ProcessStartTrace` 事件监听新进程，每行输出一个 exe 文件名（不包含完整路径）。监听就绪前已启动的进程不会补报。
- 监听运行在独立线程；退出时停止监听并释放 WMI、线程和控制台资源。
- 右键图标显示“Exit”菜单，点击后删除图标并退出程序。
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
