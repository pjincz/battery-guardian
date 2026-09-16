#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>

namespace {
constexpr wchar_t kWindowClass[] = L"BatteryGuardianTrayWindow";
constexpr UINT kTrayMessage = WM_APP + 1;
constexpr UINT kTrayId = 1;
constexpr UINT kExitCommand = 1001;
UINT g_taskbarCreated = 0;

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
    if (!AppendMenuW(menu, MF_STRING, kExitCommand, L"Exit")) {
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

    MSG message{};
    BOOL result;
    while ((result = GetMessageW(&message, nullptr, 0, 0)) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    if (result == -1) {
        DestroyWindow(window);
        return 1;
    }
    return static_cast<int>(message.wParam);
}
