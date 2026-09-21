#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#include "process_log.h"
#include <cstdio>
int failures = 0;
void Check(bool ok, const char* label) {
    printf("%s: %s\n", ok ? "PASS" : "FAIL", label);
    if (!ok) ++failures;
}
int main(int argc, char**) {
    if (argc > 1) return 0;
    SYSTEMTIME sample{};
    sample.wYear = 2026; sample.wMonth = 9; sample.wDay = 21;
    sample.wHour = 8; sample.wMinute = 2; sample.wSecond = 3; sample.wMilliseconds = 4;
    Check(process_log::FormatTime(sample) == L"2026-09-21 08:02:03.004", "Date and millisecond formatting");
    wchar_t executable[32768]{};
    GetModuleFileNameW(nullptr, executable, 32768);
    std::wstring path(executable);
    std::wstring name = path.substr(path.find_last_of(L"\\/") + 1);
    std::wstring command = L"\"" + path + L"\" --child";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION child{};
    if (!CreateProcessW(path.c_str(), command.data(), nullptr, nullptr, FALSE,
        CREATE_SUSPENDED | CREATE_NO_WINDOW, nullptr, nullptr, &startup, &child)) return 2;
    FILETIME now{}, created{}, exited{}, kernel{}, user{};
    GetSystemTimeAsFileTime(&now);
    GetProcessTimes(child.hProcess, &created, &exited, &kernel, &user);
    ULONGLONG ticks = (static_cast<ULONGLONG>(now.dwHighDateTime) << 32) | now.dwLowDateTime;
    ULONGLONG createdTicks = (static_cast<ULONGLONG>(created.dwHighDateTime) << 32) | created.dwLowDateTime;
    const auto info = process_log::Query(child.dwProcessId, ticks, name.c_str());
    Check(info.path == path && info.created == createdTicks, "Actual executable path and creation time");
    const auto message = process_log::StartMessage(child.dwProcessId, true, name.c_str(), ticks, GetCurrentProcessId(), true);
    Check(message.find(L"START pid=" + std::to_wstring(child.dwProcessId)) != std::wstring::npos
        && message.find(L"exe=\"" + path + L"\"") != std::wstring::npos
        && message.find(L"born=\"" + process_log::CreationTime(createdTicks) + L"\"") != std::wstring::npos
        && message.find(L"ppid=" + std::to_wstring(GetCurrentProcessId())) != std::wstring::npos
        && message.find(L"parent=\"" + path + L"\"") != std::wstring::npos, "Process start message includes all requested fields");
    Check(process_log::Query(child.dwProcessId, 1).path.empty(), "Recycled PID is not attributed to the old event");
    Check(process_log::Query(child.dwProcessId, ticks, L"wrong.exe").created == 0, "Mismatched executable identity rejected");
    auto unavailable = process_log::StartMessage(0, false, L"gone.exe", 0, 0, false);
    Check(unavailable.find(L"exe=\"?\"") != std::wstring::npos
        && unavailable.find(L"gone.exe") != std::wstring::npos, "Unavailable fields retain the event filename");
    const auto missingTime = process_log::StartMessage(child.dwProcessId, true, name.c_str(), 0, GetCurrentProcessId(), true);
    Check(missingTime.find(L"perr=no-child-time") != std::wstring::npos,
        "Parent diagnostic explains missing child creation time");
    Check(process_log::Query(0, ticks).error == L"OpenProcess:87",
        "Query failure identifies the API and error code");
    ResumeThread(child.hThread);
    Check(WaitForSingleObject(child.hProcess, 5000) == WAIT_OBJECT_0, "Owned test child exits normally");
    CloseHandle(child.hThread); CloseHandle(child.hProcess);
    printf("Failures: %d\n", failures);
    return failures ? 1 : 0;
}



