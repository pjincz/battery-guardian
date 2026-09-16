#pragma once
#include <windows.h>

namespace log_window {
// Create and Destroy run on the UI thread. Stop log producers before Destroy.
HWND Create(HINSTANCE instance);
void Write(const wchar_t* line);
void Destroy();
}
