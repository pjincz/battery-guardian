# Battery Guardian

[中文文档](README.zh.md)

Battery Guardian is a Windows tray utility that binds selected applications to your CPU's efficiency cores (E cores), aiming to reduce background use of performance cores and power consumption. It handles matching processes already running at startup and monitors new processes. You choose which applications to limit in `blacklist.txt`. Actual battery life improvements depend on your hardware and workload.

**Disclaimer: This project was written almost entirely by AI and has not undergone thorough human review. It may contain undiscovered bugs. Evaluate the risks before using it. Energy savings, stability, and compatibility are not guaranteed. Use it at your own risk.**

## Getting started

1. Place `BatteryGuardian.exe` and `blacklist.txt` in the same directory.
2. Launch the application and approve the Windows administrator permission prompt.
3. Right-click the tray icon, select `Edit blacklist.txt`, add the executable names you want to limit, and save the file.
4. Click `Reload blacklist.txt` to apply the rules to matching processes that are already running and those that start later.

The default list is empty, so no applications are restricted. The tray icon may appear in the taskbar's hidden icons area.

Your CPU must have E cores that Windows can identify. Unsupported hardware or configurations are skipped, with an explanation in the log.

## Configuring the list

Use UTF-8 encoding for `blacklist.txt`, with one executable filename per line:

```text
# Background applications
example.exe
another-app.exe
```

- Matching is case-insensitive and applies to all processes with the same executable name.
- Blank lines and comment lines starting with `#` are ignored.
- Full paths, wildcards, and inline comments are not supported.
- After saving, click `Reload blacklist.txt` to apply your changes.
- Emptying the list and reloading removes the current rules and attempts to restore the original settings.

Binding an application to E cores may reduce its performance. Start with background applications that do not need high performance.

## Everyday use

- **Left-click the tray icon** to show or hide the log window. Closing the window only hides it; the application continues running in the background.
- **Edit blacklist.txt** opens the list in Notepad.
- **Reload blacklist.txt** restores previously modified settings, then loads the list and applies the new rules.
- **Exit** restores the original CPU affinity of managed processes that are still running, then exits.

Successful rule application triggers a tray balloon notification. Its visibility depends on your Windows notification settings. Launching another instance displays a message that the application is already running.

Use `Exit` to close the application normally. Restoration cannot be guaranteed if the application is forcibly terminated, crashes, or loses power. Failures to apply or restore settings are recorded in the log.

## Reading the log

The log records process starts and rule application results. You can select and copy the text. Each record occupies one line: `START` indicates a process start, and `AFFINITY` indicates that CPU affinity was set.

`?` means information could not be retrieved. `err=` and `perr=` report query errors for the process and its parent, respectively. Short-lived processes may exit before they can be queried, so missing information does not necessarily mean a rule failed to apply.

## Building from source

Install Visual Studio or Build Tools with the **Desktop development with C++** workload, then run:

```bat
build.cmd
```

The executable is generated at `build\BatteryGuardian.exe`, and the list is at `build\blacklist.txt`. Rebuilding does not overwrite an existing list.

