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
cl /nologo /std:c++17 /W4 /utf-8 /EHsc /O2 main.cpp /Fo:build\main.obj /Fe:build\BatteryGuardian.exe /link /SUBSYSTEM:WINDOWS user32.lib shell32.lib
exit /b %errorlevel%
:missing
echo Install Visual Studio Build Tools with Desktop development with C++.
exit /b 1
