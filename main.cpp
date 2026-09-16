#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <wbemidl.h>
#include <cstdio>
#include <string>
#include "affinity_policy.h"

namespace {
constexpr wchar_t kWindowClass[] = L"BatteryGuardianTrayWindow";
constexpr UINT kTrayMessage = WM_APP + 1;
constexpr UINT kTrayId = 1;
constexpr UINT kExitCommand = 1001;
constexpr UINT kToggleTerminalCommand = 1002;
UINT g_taskbarCreated = 0;
HANDLE g_logOutput = INVALID_HANDLE_VALUE;
HWND g_consoleWindow = nullptr;

void Log(const wchar_t* text) {
    const std::wstring line = std::wstring(text) + L"\r\n";
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
        Log(L"Access denied. Try running this application as administrator.");
    }
}

DWORD WINAPI MonitorProcesses(void* context) {
    const HANDLE stopEvent = static_cast<HANDLE>(context);
    affinity::Policy policy;
    policy.Initialize(Log);
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
            if (count != 0 && event) {
                VARIANT name;
                VariantInit(&name);
                const HRESULT propertyResult = event->Get(L"ProcessName", 0, &name, nullptr, nullptr);
                if (SUCCEEDED(propertyResult) && name.vt == VT_BSTR && name.bstrVal) {
                    Log(name.bstrVal);
                    if (policy.Contains(name.bstrVal)) {
                        VARIANT processId;
                        VARIANT eventTime;
                        VariantInit(&processId);
                        VariantInit(&eventTime);
                        HRESULT details = event->Get(L"ProcessID", 0, &processId, nullptr, nullptr);
                        if (SUCCEEDED(details)) details = event->Get(L"TIME_CREATED", 0, &eventTime, nullptr, nullptr);
                        if (SUCCEEDED(details)) details = VariantChangeType(&eventTime, &eventTime, 0, VT_UI8);
                        if (SUCCEEDED(details) && (processId.vt == VT_I4 || processId.vt == VT_UI4)) {
                            const DWORD pid = processId.vt == VT_UI4 ? processId.ulVal : static_cast<DWORD>(processId.lVal);
                            policy.Apply(pid, name.bstrVal, eventTime.ullVal);
                        } else {
                            LogError(L"Read process start details", FAILED(details) ? details : E_UNEXPECTED);
                        }
                        VariantClear(&eventTime);
                        VariantClear(&processId);
                    }
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
    icon.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    lstrcpyW(icon.szTip, L"Battery Guardian");
    return Shell_NotifyIconW(NIM_ADD, &icon) != FALSE;
}

void RemoveTrayIcon(HWND window) {
    NOTIFYICONDATAW icon{};
    icon.cbSize = sizeof(icon);
    icon.hWnd = window;
    icon.uID = kTrayId;
    Shell_NotifyIconW(NIM_DELETE, &icon);
}

void ShowTrayMenu(HWND window) {
    POINT position{};
    if (!GetCursorPos(&position)) return;
    HMENU menu = CreatePopupMenu();
    if (!menu) return;
    const bool canToggle = g_consoleWindow && IsWindow(g_consoleWindow);
    const bool terminalVisible = canToggle && IsWindowVisible(g_consoleWindow)
        && !IsIconic(g_consoleWindow);
    if (!AppendMenuW(menu, MF_STRING | (canToggle ? MF_ENABLED : MF_GRAYED),
            kToggleTerminalCommand, terminalVisible ? L"Hide Terminal" : L"Show Terminal")
        || !AppendMenuW(menu, MF_SEPARATOR, 0, nullptr)
        || !AppendMenuW(menu, MF_STRING, kExitCommand, L"Exit")) {
        DestroyMenu(menu);
        return;
    }

    // Foreground ownership lets a click outside the menu dismiss it normally.
    SetForegroundWindow(window);
    const UINT command = static_cast<UINT>(TrackPopupMenuEx(
        menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON,
        position.x, position.y, window, nullptr));
    DestroyMenu(menu);
    PostMessageW(window, WM_NULL, 0, 0);
    if (command == kToggleTerminalCommand && canToggle) {
        // Keep the console allocated so monitoring and buffered output continue.
        ShowWindow(g_consoleWindow, terminalVisible ? SW_HIDE : SW_RESTORE);
        if (!terminalVisible) SetForegroundWindow(g_consoleWindow);
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
    case kTrayMessage:
        if (wParam == kTrayId && lParam == WM_RBUTTONUP) ShowTrayMenu(window);
        return 0;
    case WM_CLOSE:
        DestroyWindow(window);
        return 0;
    case WM_DESTROY:
        RemoveTrayIcon(window);
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(window, message, wParam, lParam);
    }
}
} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    g_taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = WindowProc;
    windowClass.hInstance = instance;
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
    const bool ownsConsole = AllocConsole() != FALSE;
    g_logOutput = redirected ? standardOutput : GetStdHandle(STD_OUTPUT_HANDLE);
    if (!ownsConsole && !redirected && (!g_logOutput || g_logOutput == INVALID_HANDLE_VALUE)) {
        MessageBoxW(window, L"Failed to open the debug console.", L"Battery Guardian", MB_OK | MB_ICONERROR);
        DestroyWindow(window);
        return 1;
    }
    if (ownsConsole) {
        SetConsoleTitleW(L"Battery Guardian - Process Monitor");
        // Only control our own console, never a terminal inherited from a caller.
        g_consoleWindow = GetConsoleWindow();
    }

    HANDLE stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE monitorThread = nullptr;
    if (stopEvent) {
        monitorThread = CreateThread(nullptr, 0, MonitorProcesses, stopEvent, 0, nullptr);
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
    }
    if (stopEvent) CloseHandle(stopEvent);
    g_consoleWindow = nullptr;
    if (ownsConsole) FreeConsole();
    return result == -1 ? 1 : static_cast<int>(message.wParam);
}
