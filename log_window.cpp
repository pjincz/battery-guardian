#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#include "log_window.h"
#include "app_icon.h"
#include <string>

namespace log_window {
namespace {
constexpr wchar_t kClassName[] = L"BatteryGuardianLogWindow";
constexpr UINT kFlushLogs = WM_APP + 20;
constexpr size_t kHistoryLimit = 256 * 1024;
HWND window = nullptr;
HWND edit = nullptr;
SRWLOCK lock = SRWLOCK_INIT;
std::wstring pending;
bool flushPosted = false;

void Flush() {
    std::wstring text;
    AcquireSRWLockExclusive(&lock);
    text.swap(pending);
    flushPosted = false;
    ReleaseSRWLockExclusive(&lock);
    if (text.empty() || !edit) return;

    const size_t length = static_cast<size_t>(GetWindowTextLengthW(edit));
    if (length + text.size() > kHistoryLimit) {
        const size_t remove = length + text.size() - kHistoryLimit;
        SendMessageW(edit, EM_SETSEL, 0, static_cast<LPARAM>(remove));
        SendMessageW(edit, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(L""));
    }
    const int end = GetWindowTextLengthW(edit);
    SendMessageW(edit, EM_SETSEL, static_cast<WPARAM>(end), static_cast<LPARAM>(end));
    SendMessageW(edit, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(text.c_str()));
    SendMessageW(edit, EM_SCROLLCARET, 0, 0);
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE:
        edit = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL
            | WS_HSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | ES_AUTOHSCROLL,
            0, 0, 0, 0, hwnd, nullptr,
            reinterpret_cast<CREATESTRUCTW*>(lParam)->hInstance, nullptr);
        if (!edit) return -1;
        SendMessageW(edit, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(ANSI_FIXED_FONT)), TRUE);
        SendMessageW(edit, EM_SETLIMITTEXT, kHistoryLimit, 0);
        return 0;
    case WM_SIZE:
        if (edit) MoveWindow(edit, 0, 0, LOWORD(lParam), HIWORD(lParam), TRUE);
        return 0;
    case WM_SETFOCUS:
        if (edit) SetFocus(edit);
        return 0;
    case WM_CTLCOLORSTATIC:
        SetTextColor(reinterpret_cast<HDC>(wParam), RGB(220, 220, 220));
        SetBkColor(reinterpret_cast<HDC>(wParam), RGB(0, 0, 0));
        return reinterpret_cast<LRESULT>(GetStockObject(BLACK_BRUSH));
    case kFlushLogs:
        Flush();
        return 0;
    case WM_CLOSE:
        // The close button and Alt+F4 hide only this window.
        ShowWindow(hwnd, SW_HIDE);
        return 0;
    case WM_DESTROY:
        edit = nullptr;
        return 0;
    default:
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }
}
} // namespace

HWND Create(HINSTANCE instance) {
    WNDCLASSEXW type{};
    type.cbSize = sizeof(type);
    type.lpfnWndProc = WindowProc;
    type.hInstance = instance;
    type.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    type.hIcon = LoadAppIcon(instance, GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON));
    type.hIconSm = LoadAppIcon(instance, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON));
    type.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    type.lpszClassName = kClassName;
    if (!RegisterClassExW(&type) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return nullptr;
    window = CreateWindowExW(0, kClassName, L"Battery Guardian - Terminal", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 900, 520, nullptr, nullptr, instance, nullptr);
    return window;
}

void Write(const wchar_t* line) {
    AcquireSRWLockExclusive(&lock);
    if (window) {
        pending += line;
        if (pending.size() > kHistoryLimit) pending.erase(0, pending.size() - kHistoryLimit);
        if (!flushPosted) flushPosted = PostMessageW(window, kFlushLogs, 0, 0) != FALSE;
    }
    ReleaseSRWLockExclusive(&lock);
}

void Destroy() {
    AcquireSRWLockExclusive(&lock);
    const HWND oldWindow = window;
    window = nullptr;
    pending.clear();
    flushPosted = false;
    ReleaseSRWLockExclusive(&lock);
    if (oldWindow) DestroyWindow(oldWindow);
}
} // namespace log_window

