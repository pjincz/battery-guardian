@echo off
setlocal
cd /d "%~dp0"
where cl >nul 2>nul
if not errorlevel 1 goto compile
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" goto missing
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSINSTALL=%%i"
if not defined VSINSTALL goto missing
call "%VSINSTALL%\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64
if errorlevel 1 exit /b 1
:compile
if not exist build mkdir build
rc /nologo /fo build\BatteryGuardian.res BatteryGuardian.rc
if errorlevel 1 exit /b %errorlevel%
cl /nologo /std:c++17 /W4 /utf-8 /EHsc /O2 main.cpp affinity_policy.cpp log_window.cpp build\BatteryGuardian.res /Fo:build\ /Fe:build\BatteryGuardian.exe /link /SUBSYSTEM:WINDOWS /MANIFEST:NO advapi32.lib user32.lib gdi32.lib shell32.lib ole32.lib oleaut32.lib wbemuuid.lib
if errorlevel 1 exit /b %errorlevel%
if not exist build\blacklist.txt copy /y blacklist.txt build\blacklist.txt >nul
exit /b %errorlevel%
:missing
echo Install Visual Studio Build Tools with Desktop development with C++.
exit /b 1

