# Battery Guardian

纯 C++ / Win32 API 托盘程序，无第三方框架或依赖库。

- 启动后显示系统默认应用程序托盘图标，不显示主窗口或控制台。
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
