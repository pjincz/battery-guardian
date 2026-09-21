#pragma once
#include <windows.h>
#include <cstdio>
#include <string>

namespace process_log {
inline std::wstring FormatTime(const SYSTEMTIME& time) {
    wchar_t text[32]{};
    swprintf_s(text, L"%04u-%02u-%02u %02u:%02u:%02u.%03u",
        time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond, time.wMilliseconds);
    return text;
}
inline std::wstring Timestamp() {
    SYSTEMTIME now{};
    GetLocalTime(&now);
    return FormatTime(now);
}
inline std::wstring CreationTime(ULONGLONG ticks) {
    if (!ticks) return L"?";
    FILETIME ft{static_cast<DWORD>(ticks), static_cast<DWORD>(ticks >> 32)};
    SYSTEMTIME utc{}, local{};
    if (!FileTimeToSystemTime(&ft, &utc)
        || !SystemTimeToTzSpecificLocalTime(nullptr, &utc, &local)) return L"?";
    return FormatTime(local);
}
struct ProcessInfo {
    std::wstring path;
    ULONGLONG created = 0;
    std::wstring error;
};
inline std::wstring QueryError(const wchar_t* operation, DWORD error) {
    return std::wstring(operation) + L":" + std::to_wstring(error);
}
inline ProcessInfo Query(DWORD pid, ULONGLONG latestCreation, const wchar_t* expectedName = nullptr) {
    ProcessInfo info;
    if (!latestCreation) {
        info.error = L"no-event-time";
        return info;
    }
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) {
        const DWORD error = GetLastError();
        info.error = QueryError(L"OpenProcess", error);
        return info;
    }
    FILETIME created{}, exited{}, kernel{}, user{};
    if (!GetProcessTimes(process, &created, &exited, &kernel, &user)) {
        info.error = QueryError(L"GetProcessTimes", GetLastError());
        CloseHandle(process);
        return info;
    }
    info.created = (static_cast<ULONGLONG>(created.dwHighDateTime) << 32) | created.dwLowDateTime;
    if (info.created > latestCreation) {
        info.created = 0;
        info.error = L"pid-reused";
        CloseHandle(process);
        return info;
    }
    std::wstring image(32768, L'\0');
    DWORD length = static_cast<DWORD>(image.size());
    if (QueryFullProcessImageNameW(process, 0, image.data(), &length)) {
        image.resize(length);
        const std::wstring filename = image.substr(image.find_last_of(L"\\/") + 1);
        if (expectedName && CompareStringOrdinal(filename.c_str(), -1, expectedName, -1, TRUE) != CSTR_EQUAL) {
            info.error = L"identity-changed";
            info.created = 0;
        } else {
            info.path = std::move(image);
        }
    } else {
        info.error = QueryError(L"QueryFullProcessImageNameW", GetLastError());
    }
    CloseHandle(process);
    return info;
}
inline std::wstring StartMessage(DWORD pid, bool pidKnown, const wchar_t* name,
    ULONGLONG eventTime, DWORD parentPid, bool parentKnown) {
    const ProcessInfo process = pidKnown ? Query(pid, eventTime, name)
        : ProcessInfo{L"", 0, L"no-pid"};
    // A live parent must predate its child; never attribute a recycled PID.
    const ProcessInfo parent = parentKnown ? (process.created ? Query(parentPid, process.created)
        : ProcessInfo{L"", 0, L"no-child-time"})
        : ProcessInfo{L"", 0, L"no-ppid"};
    std::wstring text = L"START pid=" + (pidKnown ? std::to_wstring(pid) : L"?")
        + L" exe=\"" + (process.path.empty() ? L"?" : process.path) + L"\""
        + L" born=\"" + CreationTime(process.created) + L"\""
        + L" ppid=" + (parentKnown ? std::to_wstring(parentPid) : L"?")
        + L" parent=\"" + (parent.path.empty() ? L"?" : parent.path) + L"\"";
    if (process.path.empty()) text += L" name=\"" + std::wstring(name) + L"\"";
    if (!process.error.empty()) text += L" err=" + process.error;
    if (!parent.error.empty()) text += L" perr=" + parent.error;
    return text;
}
} // namespace process_log

