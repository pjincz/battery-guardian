#pragma once
#include <windows.h>
#include "resource.h"

inline HICON LoadAppIcon(HINSTANCE instance, int width, int height) {
    // Shared resource icons are owned by Windows for the process lifetime.
    return static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(IDI_APP_ICON),
        IMAGE_ICON, width, height, LR_SHARED));
}
