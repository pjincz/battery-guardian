#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <wbemidl.h>
#include <cstdio>
#include <string>
#include <atomic>
#include "affinity_policy.h"
#include "log_window.h"
#include "app_icon.h"
#include "process_log.h"

namespace {
constexpr wchar_t kWindowClass[] = L"BatteryGuardianTrayWindow";
constexpr UINT kTrayMessage = WM_APP + 1;
constexpr UINT kAffinityNoticeMessage = WM_APP + 2;
constexpr UINT_PTR kAffinityNoticeTimer = 1;
constexpr UINT kTrayId = 1;
constexpr UINT kExitCommand = 1001;
constexpr UINT kToggleTerminalCommand = 1002;
constexpr UINT kReloadBlacklistCommand = 1003;
constexpr UINT kEditBlacklistCommand = 1004;
std::atomic<bool> g_reloadRequested{false};
HANDLE g_monitorThread = nullptr;
UINT g_taskbarCreated = 0;
HANDLE g_logOutput = INVALID_HANDLE_VALUE;
HWND g_consoleWindow = nullptr;
SRWLOCK g_noticeLock = SRWLOCK_INIT;
HWND g_noticeWindow = nullptr;
ULONG g_noticeCount = 0;
std::wstring g_noticeFirst;
bool g_noticePending = false;

void QueueAffinityNotice(const wchar_t* name, DWORD pid, DWORD_PTR mask) {
    AcquireSRWLockExclusive(&g_noticeLock);
    if (g_noticeWindow) {
        if (g_noticeCount == 0) {
            wchar_t details[96]{};
            swprintf_s(details, L" (PID %lu)\nBound to E cores (0x%llX).", pid,
                static_cast<unsigned long long>(mask));
            g_noticeFirst = std::wstring(name).substr(0, 100) + details;
        }
        ++g_noticeCount;
        if (!g_noticePending) {
            g_noticePending = PostMessageW(g_noticeWindow, kAffinityNoticeMessage, 0, 0) != FALSE;
        }
    }
    ReleaseSRWLockExclusive(&g_noticeLock);
}

void Log(const wchar_t* text) {
    const std::wstring line = L"[" + process_log::Timestamp() + L"] " + text + L"\r\n";
    log_window::Write(line.c_str());
    if (!g_logOutput || g_logOutput == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    DWORD mode = 0;
    if (GetConsoleMode(g_logOutput, &mode)) {
        WriteConsoleW(g_logOutput, line.data(), static_cast<DWORD>(line.size()), &written, nullptr);
    } else {
        const int size = WideCharToMultiByte(CP_UTF8, 0, line.data(),
            static_cast<int>(line.size()), nullptr, 0, nullptr, nullptr);
        std::string bytes(size, '\0');
        WideCharToMultiByte(CP_UTF8, 0, line.data(), static_cast<int>(line.size()),
            bytes.data(), size, nullptr, nullptr);
        WriteFile(g_logOutput, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr);
    }
}

void LogError(const wchar_t* operation, HRESULT error) {
    wchar_t message[256]{};
    swprintf_s(message, L"%ls failed (HRESULT 0x%08lX).", operation, static_cast<unsigned long>(error));
    Log(message);
    if (error == E_ACCESSDENIED || error == WBEM_E_ACCESS_DENIED) {
        Log(L"Access denied. Check WMI permissions or system security policy.");
    }
}

bool ReadEventPid(IWbemClassObject* event, const wchar_t* property, DWORD& value) {
    VARIANT data;
    VariantInit(&data);
    const HRESULT result = event->Get(property, 0, &data, nullptr, nullptr);
    const bool valid = SUCCEEDED(result) && (data.vt == VT_I4 || data.vt == VT_UI4);
    if (valid) value = data.vt == VT_UI4 ? data.ulVal : static_cast<DWORD>(data.lVal);
    VariantClear(&data);
    return valid;
}

DWORD WINAPI MonitorProcesses(void* context) {
    const HANDLE stopEvent = static_cast<HANDLE>(context);
    affinity::Policy policy;
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr)) {
        LogError(L"CoInitializeEx", hr);
        return 1;
    }

    IWbemLocator* locator = nullptr;
    IWbemServices* services = nullptr;
    IEnumWbemClassObject* events = nullptr;
    const auto listen = [&]() -> HRESULT {
        HRESULT result = CoInitializeSecurity(nullptr, -1, nullptr, nullptr,
            RPC_C_AUTHN_LEVEL_DEFAULT, RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE, nullptr);
        if (FAILED(result) && result != RPC_E_TOO_LATE) return result;
        result = CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER,
            IID_IWbemLocator, reinterpret_cast<void**>(&locator));
        if (FAILED(result)) return result;

        BSTR nameSpace = SysAllocString(L"ROOT\\CIMV2");
        if (!nameSpace) return E_OUTOFMEMORY;
        result = locator->ConnectServer(nameSpace, nullptr, nullptr, nullptr,
            WBEM_FLAG_CONNECT_USE_MAX_WAIT, nullptr, nullptr, &services);
        SysFreeString(nameSpace);
        if (FAILED(result)) return result;
        result = CoSetProxyBlanket(services, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE,
            nullptr, RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE);
        if (FAILED(result)) return result;

        BSTR language = SysAllocString(L"WQL");
        BSTR query = SysAllocString(L"SELECT * FROM Win32_ProcessStartTrace");
        if (!language || !query) {
            SysFreeString(language);
            SysFreeString(query);
            return E_OUTOFMEMORY;
        }
        result = services->ExecNotificationQuery(language, query,
            WBEM_FLAG_RETURN_IMMEDIATELY | WBEM_FLAG_FORWARD_ONLY, nullptr, &events);
        SysFreeString(language);
        SysFreeString(query);
        if (FAILED(result)) return result;

        // Subscribe first so a single load-time scan leaves no startup event gap.
        policy.Initialize(Log, QueueAffinityNotice);
        Log(L"Listening for process starts. Use the tray Exit menu to stop.");
        while (WaitForSingleObject(stopEvent, 0) == WAIT_TIMEOUT) {
            IWbemClassObject* event = nullptr;
            ULONG count = 0;
            // Wait for queued events, not periodic snapshots of the process list.
            result = events->Next(250, 1, &event, &count);
            if (FAILED(result)) {
                if (event) event->Release();
                return result;
            }
            if (WaitForSingleObject(stopEvent, 0) == WAIT_OBJECT_0) {
                if (event) event->Release();
                return S_OK;
            }
            // All policy changes run on this thread, between event deliveries.
            if (g_reloadRequested.exchange(false)) policy.Reload();
            if (count != 0 && event) {
                VARIANT name;
                VariantInit(&name);
                const HRESULT propertyResult = event->Get(L"ProcessName", 0, &name, nullptr, nullptr);
                if (SUCCEEDED(propertyResult) && name.vt == VT_BSTR && name.bstrVal) {
                    DWORD pid = 0, parentPid = 0;
                    const bool pidKnown = ReadEventPid(event, L"ProcessID", pid);
                    const bool parentKnown = ReadEventPid(event, L"ParentProcessID", parentPid);
                    VARIANT eventTime;
                    VariantInit(&eventTime);
                    HRESULT details = event->Get(L"TIME_CREATED", 0, &eventTime, nullptr, nullptr);
                    if (SUCCEEDED(details)) details = VariantChangeType(&eventTime, &eventTime, 0, VT_UI8);
                    const ULONGLONG ticks = SUCCEEDED(details) ? eventTime.ullVal : 0;
                    Log(process_log::StartMessage(pid, pidKnown, name.bstrVal, ticks, parentPid, parentKnown).c_str());
                    if (policy.Contains(name.bstrVal)) {
                        if (pidKnown && ticks) policy.Apply(pid, name.bstrVal, ticks);
                        else LogError(L"Read process start details", FAILED(details) ? details : E_UNEXPECTED);
                    }
                    VariantClear(&eventTime);
                } else {
                    LogError(L"Read ProcessName", FAILED(propertyResult) ? propertyResult : E_UNEXPECTED);
                }
                VariantClear(&name);
            }
            if (event) event->Release();
            if (result == WBEM_S_FALSE) return E_UNEXPECTED;
        }
        return S_OK;
    };

    hr = listen();
    if (FAILED(hr)) LogError(L"Process monitoring", hr);
    // Releasing the enumerator cancels the WMI event subscription.
    if (events) events->Release();
    if (services) services->Release();
    if (locator) locator->Release();
    CoUninitialize();
    Log(L"Process monitoring stopped.");
    return FAILED(hr) ? 1 : 0;
}

bool AddTrayIcon(HWND window) {
    NOTIFYICONDATAW icon{};
    icon.cbSize = sizeof(icon);
    icon.hWnd = window;
    icon.uID = kTrayId;
    icon.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    icon.uCallbackMessage = kTrayMessage;
    icon.hIcon = LoadAppIcon(GetModuleHandleW(nullptr), GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON));
    if (!icon.hIcon) return false;
    lstrcpyW(icon.szTip, L"Battery Guardian");
    return Shell_NotifyIconW(NIM_ADD, &icon) != FALSE;
}

void ShowAffinityNotice(HWND window) {
    KillTimer(window, kAffinityNoticeTimer);
    AcquireSRWLockExclusive(&g_noticeLock);
    const ULONG count = g_noticeCount;
    std::wstring text;
    text.swap(g_noticeFirst);
    g_noticeCount = 0;
    g_noticePending = false;
    ReleaseSRWLockExclusive(&g_noticeLock);
    if (!count) return;
    if (count > 1) text += L"\nAlso applied to " + std::to_wstring(count - 1) + L" other process(es).";

    NOTIFYICONDATAW icon{};
    icon.cbSize = sizeof(icon);
    icon.hWnd = window;
    icon.uID = kTrayId;
    icon.uFlags = NIF_INFO;
    icon.dwInfoFlags = NIIF_INFO | NIIF_NOSOUND | NIIF_RESPECT_QUIET_TIME;
    lstrcpyW(icon.szInfoTitle, L"Battery Guardian - E-core binding");
    lstrcpynW(icon.szInfo, text.c_str(), ARRAYSIZE(icon.szInfo));
    if (!Shell_NotifyIconW(NIM_MODIFY, &icon)) Log(L"Failed to submit the affinity notification.");
}

void RemoveTrayIcon(HWND window) {
    NOTIFYICONDATAW icon{};
    icon.cbSize = sizeof(icon);
    icon.hWnd = window;
    icon.uID = kTrayId;
    Shell_NotifyIconW(NIM_DELETE, &icon);
}

void ToggleTerminal() {
    if (!g_consoleWindow || !IsWindow(g_consoleWindow)) return;
    const bool visible = IsWindowVisible(g_consoleWindow) && !IsIconic(g_consoleWindow);
    // Keep the log window alive so hidden output is retained.
    ShowWindow(g_consoleWindow, visible ? SW_HIDE : SW_RESTORE);
    if (!visible) SetForegroundWindow(g_consoleWindow);
}

void EditBlacklist(HWND window) {
    const auto reportFailure = [&](DWORD error) {
        LogError(L"Open blacklist.txt in Notepad", HRESULT_FROM_WIN32(error));
        MessageBoxW(window, L"Failed to open blacklist.txt in Notepad. See the terminal for details.",
            L"Battery Guardian", MB_OK | MB_ICONERROR);
    };
    std::wstring path(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (!length || length >= path.size()) {
        reportFailure(length ? ERROR_INSUFFICIENT_BUFFER : GetLastError());
        return;
    }
    path.resize(length);
    path.resize(path.find_last_of(L"\\/") + 1);
    path += L"blacklist.txt";

    std::wstring notepad(32768, L'\0');
    const UINT systemLength = GetSystemDirectoryW(notepad.data(), static_cast<UINT>(notepad.size()));
    if (!systemLength || systemLength >= notepad.size()) {
        reportFailure(systemLength ? ERROR_INSUFFICIENT_BUFFER : GetLastError());
        return;
    }
    notepad.resize(systemLength);
    notepad += L"\\notepad.exe";
    // Use explicit paths and quote both arguments for folders containing spaces.
    std::wstring command = L"\"" + notepad + L"\" \"" + path + L"\"";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_SHOWNORMAL;
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(notepad.c_str(), command.data(), nullptr, nullptr, FALSE,
        0, nullptr, nullptr, &startup, &process)) {
        reportFailure(GetLastError());
        return;
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
}

void ShowTrayMenu(HWND window) {
    POINT position{};
    if (!GetCursorPos(&position)) return;
    HMENU menu = CreatePopupMenu();
    if (!menu) return;
    const bool canToggle = g_consoleWindow && IsWindow(g_consoleWindow);
    const bool terminalVisible = canToggle && IsWindowVisible(g_consoleWindow)
        && !IsIconic(g_consoleWindow);
    const bool canReload = g_monitorThread
        && WaitForSingleObject(g_monitorThread, 0) == WAIT_TIMEOUT;
    if (!AppendMenuW(menu, MF_STRING | (canToggle ? MF_ENABLED : MF_GRAYED),
            kToggleTerminalCommand, terminalVisible ? L"Hide Terminal" : L"Show Terminal")
        || !AppendMenuW(menu, MF_STRING, kEditBlacklistCommand, L"Edit blacklist.txt")
        || !AppendMenuW(menu, MF_STRING | (canReload ? MF_ENABLED : MF_GRAYED),
            kReloadBlacklistCommand, L"Reload blacklist.txt")
        || !AppendMenuW(menu, MF_SEPARATOR, 0, nullptr)
        || !AppendMenuW(menu, MF_STRING, kExitCommand, L"Exit")) {
        DestroyMenu(menu);
        return;
    }

    SetMenuDefaultItem(menu, kToggleTerminalCommand, FALSE);

    // Foreground ownership lets a click outside the menu dismiss it normally.
    SetForegroundWindow(window);
    const UINT command = static_cast<UINT>(TrackPopupMenuEx(
        menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON,
        position.x, position.y, window, nullptr));
    DestroyMenu(menu);
    PostMessageW(window, WM_NULL, 0, 0);
    if (command == kToggleTerminalCommand) ToggleTerminal();
    if (command == kEditBlacklistCommand) EditBlacklist(window);
    if (command == kReloadBlacklistCommand && canReload) {
        g_reloadRequested.store(true);
        Log(L"Blacklist reload requested.");
    }
    if (command == kExitCommand) DestroyWindow(window);
}

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    // Explorer recreates its notification area after a restart.
    if (g_taskbarCreated != 0 && message == g_taskbarCreated) {
        AddTrayIcon(window);
        return 0;
    }
    switch (message) {
    case WM_CREATE:
        return AddTrayIcon(window) ? 0 : -1;
    case kAffinityNoticeMessage:
        // Collect startup scans and multi-process launches into one notification.
        if (!SetTimer(window, kAffinityNoticeTimer, 1000, nullptr)) ShowAffinityNotice(window);
        return 0;
    case WM_TIMER:
        if (wParam == kAffinityNoticeTimer) ShowAffinityNotice(window);
        return 0;
    case kTrayMessage:
        if (wParam == kTrayId) {
            if (lParam == WM_LBUTTONUP) ToggleTerminal();
            else if (lParam == WM_RBUTTONUP) ShowTrayMenu(window);
            else if (lParam == NIN_BALLOONUSERCLICK && g_consoleWindow) {
                ShowWindow(g_consoleWindow, SW_RESTORE);
                SetForegroundWindow(g_consoleWindow);
            }
        }
        return 0;
    case WM_CLOSE:
        DestroyWindow(window);
        return 0;
    case WM_DESTROY:
        AcquireSRWLockExclusive(&g_noticeLock);
        g_noticeWindow = nullptr;
        g_noticeCount = 0;
        g_noticeFirst.clear();
        g_noticePending = false;
        ReleaseSRWLockExclusive(&g_noticeLock);
        KillTimer(window, kAffinityNoticeTimer);
        RemoveTrayIcon(window);
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(window, message, wParam, lParam);
    }
}
} // namespace

// Opening dwm.exe with PROCESS_SET_INFORMATION failed with ERROR_ACCESS_DENIED
// even when running as administrator. Elevation alone does not enable SeDebugPrivilege;
// our diagnostic confirmed that enabling it granted the required access on the tested system.
// Enable it before monitoring/scanning so affinity changes can reach such processes.
// This does not bypass protected-process restrictions; failure is logged and monitoring continues.
static void EnableDebugPrivilege() {
    HANDLE token = nullptr;
    const wchar_t* operation = L"OpenProcessToken";
    DWORD error = ERROR_SUCCESS;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token)) {
        error = GetLastError();
    } else {
        TOKEN_PRIVILEGES privileges{};
        privileges.PrivilegeCount = 1;
        operation = L"LookupPrivilegeValue";
        if (!LookupPrivilegeValueW(nullptr, SE_DEBUG_NAME, &privileges.Privileges[0].Luid)) {
            error = GetLastError();
        } else {
            privileges.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
            operation = L"AdjustTokenPrivileges";
            SetLastError(ERROR_SUCCESS);
            const BOOL adjusted = AdjustTokenPrivileges(token, FALSE, &privileges, 0, nullptr, nullptr);
            error = GetLastError();
            // TRUE can still mean ERROR_NOT_ALL_ASSIGNED: the token lacks this privilege.
            if (!adjusted && error == ERROR_SUCCESS) error = ERROR_GEN_FAILURE;
        }
        CloseHandle(token);
    }
    if (error == ERROR_SUCCESS) {
        Log(L"PRIVILEGE name=SeDebugPrivilege enabled=1");
    } else {
        Log((L"PRIVILEGE name=SeDebugPrivilege enabled=0 err=" + std::wstring(operation)
            + L":" + std::to_wstring(error)).c_str());
    }
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    // Keep the named mutex alive until all shutdown cleanup has completed.
    struct InstanceMutex {
        HANDLE handle;
        ~InstanceMutex() { if (handle) CloseHandle(handle); }
    } instanceMutex{CreateMutexW(nullptr, FALSE, L"Local\\BatteryGuardian.SingleInstance")};
    const DWORD mutexError = GetLastError();
    if (!instanceMutex.handle) {
        MessageBoxW(nullptr, L"Failed to create the single-instance mutex.",
            L"Battery Guardian", MB_OK | MB_ICONERROR);
        return 1;
    }
    if (mutexError == ERROR_ALREADY_EXISTS) {
        CloseHandle(instanceMutex.handle);
        instanceMutex.handle = nullptr;
        MessageBoxW(nullptr, L"Battery Guardian is already running. Click OK to exit this instance.",
            L"Battery Guardian", MB_OK | MB_ICONINFORMATION);
        return 0;
    }

    g_taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = WindowProc;
    windowClass.hInstance = instance;
    windowClass.hIcon = LoadAppIcon(instance, GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON));
    windowClass.hIconSm = LoadAppIcon(instance, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON));
    windowClass.lpszClassName = kWindowClass;
    if (!RegisterClassExW(&windowClass)) {
        MessageBoxW(nullptr, L"Failed to register the window class.", L"Battery Guardian", MB_OK | MB_ICONERROR);
        return 1;
    }

    // A hidden top-level window receives tray callbacks and Explorer broadcasts.
    HWND window = CreateWindowExW(0, kWindowClass, L"Battery Guardian", WS_OVERLAPPED,
        0, 0, 0, 0, nullptr, nullptr, instance, nullptr);
    if (!window) {
        MessageBoxW(nullptr, L"Failed to initialize the tray application.", L"Battery Guardian", MB_OK | MB_ICONERROR);
        return 1;
    }

    // Preserve redirected output for command-line diagnostics and integration checks.
    HANDLE standardOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    const bool redirected = standardOutput && standardOutput != INVALID_HANDLE_VALUE
        && GetFileType(standardOutput) != FILE_TYPE_UNKNOWN;
    g_logOutput = redirected ? standardOutput : INVALID_HANDLE_VALUE;
    g_consoleWindow = log_window::Create(instance);
    if (!g_consoleWindow) {
        MessageBoxW(window, L"Failed to create the terminal window.", L"Battery Guardian", MB_OK | MB_ICONERROR);
        DestroyWindow(window);
        return 1;
    }
    ShowWindow(g_consoleWindow, SW_SHOW);
    EnableDebugPrivilege();

    AcquireSRWLockExclusive(&g_noticeLock);
    g_noticeWindow = window;
    ReleaseSRWLockExclusive(&g_noticeLock);

    HANDLE stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE monitorThread = nullptr;
    if (stopEvent) {
        monitorThread = CreateThread(nullptr, 0, MonitorProcesses, stopEvent, 0, nullptr);
        g_monitorThread = monitorThread;
        if (!monitorThread) LogError(L"CreateThread", HRESULT_FROM_WIN32(GetLastError()));
    } else {
        LogError(L"CreateEvent", HRESULT_FROM_WIN32(GetLastError()));
    }

    MSG message{};
    BOOL result;
    while ((result = GetMessageW(&message, nullptr, 0, 0)) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    if (result == -1) DestroyWindow(window);
    if (monitorThread) {
        SetEvent(stopEvent);
        WaitForSingleObject(monitorThread, INFINITE);
        CloseHandle(monitorThread);
        g_monitorThread = nullptr;
    }
    if (stopEvent) CloseHandle(stopEvent);
    g_consoleWindow = nullptr;
    log_window::Destroy();
    return result == -1 ? 1 : static_cast<int>(message.wParam);
}



