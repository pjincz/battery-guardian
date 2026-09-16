#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#include "log_window.h"
#include <cstdio>
#include <string>

namespace {
int failures = 0;
void Check(bool condition, const char* label) {
    printf("%s: %s\n", condition ? "PASS" : "FAIL", label);
    if (!condition) ++failures;
}
void Pump() {
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        Check(message.message != WM_QUIT, "Closing the terminal did not request application exit");
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
}
DWORD WINAPI WriteHiddenLog(void*) {
    log_window::Write(L"Written while hidden.\r\n");
    return 0;
}
}

int main() {
    HWND window = log_window::Create(GetModuleHandleW(nullptr));
    Check(window != nullptr, "Create native log window");
    if (!window) return 1;
    ShowWindow(window, SW_SHOWNOACTIVATE);
    log_window::Write(L"Before closing.\r\n");
    Pump();
    SendMessageW(window, WM_SYSCOMMAND, SC_CLOSE, 0);
    Check(IsWindow(window) && !IsWindowVisible(window), "Title-bar Close hides the existing window");
    HANDLE worker = CreateThread(nullptr, 0, WriteHiddenLog, nullptr, 0, nullptr);
    Check(worker != nullptr, "Create background log producer");
    if (worker) {
        WaitForSingleObject(worker, INFINITE);
        CloseHandle(worker);
    }
    Pump();
    HWND edit = FindWindowExW(window, nullptr, L"EDIT", nullptr);
    wchar_t text[256]{};
    GetWindowTextW(edit, text, 256);
    Check(std::wstring(text) == L"Before closing.\r\nWritten while hidden.\r\n",
        "Logs are retained and updated while hidden");
    ShowWindow(window, SW_SHOWNOACTIVATE);
    Check(IsWindowVisible(window) != FALSE, "Show Terminal restores the window");
    SendMessageW(window, WM_CLOSE, 0, 0);
    Check(IsWindow(window) && !IsWindowVisible(window), "Repeated close keeps the window alive");
    log_window::Write(std::wstring(300000, L'x').c_str());
    Pump();
    Check(GetWindowTextLengthW(edit) <= 256 * 1024, "Log history is bounded");
    log_window::Destroy();
    Check(!IsWindow(window), "Explicit shutdown destroys the log window");
    printf("Failures: %d\n", failures);
    return failures ? 1 : 0;
}
