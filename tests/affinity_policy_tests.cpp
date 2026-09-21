#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#include "affinity_policy.h"
#include <cstdio>
#include <cstdlib>

namespace {
int failures = 0;
unsigned int appliedNotifications = 0;
std::wstring logs;

void OnApplied(const wchar_t*, DWORD, DWORD_PTR) {
    ++appliedNotifications;
}

void Check(bool condition, const char* name) {
    printf("%s: %s\n", condition ? "PASS" : "FAIL", name);
    if (!condition) ++failures;
}

void Capture(const wchar_t* message) {
    logs += message;
    logs += L'\n';
    printf("LOG: %ls\n", message);
}

ULONGLONG Now() {
    FILETIME time{};
    GetSystemTimeAsFileTime(&time);
    return (static_cast<ULONGLONG>(time.dwHighDateTime) << 32) | time.dwLowDateTime;
}

void CheckParser() {
    std::vector<std::wstring> names;
    std::wstring error;
    Check(affinity::ParseBlacklist("\xEF\xBB\xBF# Header\r\n  Editor.EXE \r\neditor.exe\n\nmy app.exe\n", names, error)
        && names.size() == 2 && affinity::Matches(names, L"EDITOR.exe")
        && affinity::Matches(names, L"my app.exe") && !affinity::Matches(names, L"editor.exe.bak"),
        "UTF-8 BOM, CRLF, trimming, comments, duplicates and exact case-insensitive names");
    Check(affinity::ParseBlacklist("\xE6\xB5\x8B\xE8\xAF\x95.exe\n", names, error)
        && affinity::Matches(names, L"\u6D4B\u8BD5.exe"), "UTF-8 executable filename");
    Check(affinity::ParseBlacklist("", names, error) && names.empty(), "Empty blacklist");
    for (const std::string input : {"good.exe\nC:\\bad.exe", "*.exe", "name.dll", "name.exe # comment", "\xFF\xFE"}) {
        Check(!affinity::ParseBlacklist(input, names, error) && names.empty() && !error.empty(),
            "Invalid input rejects the complete blacklist");
    }
    Check(!affinity::ParseBlacklist(std::string("bad\0name.exe", 12), names, error), "Embedded NUL rejected");
}

void CheckTopology() {
    Check(affinity::SelectEfficiencyMask({{8, 0, 3}, {0, 0, 4}, {0, 0, 8}}) == 12,
        "E-core selection excludes high-performance SMT cores");
    Check(affinity::SelectEfficiencyMask({{10, 0, 1}, {4, 0, 2}, {2, 0, 4}}) == 4,
        "Three classes select only the lowest class");
    Check(affinity::SelectEfficiencyMask({{0, 0, 1}, {0, 0, 2}}) == 0
        && affinity::SelectEfficiencyMask({{4, 0, 1}, {4, 0, 2}}) == 0,
        "Uniform core classes do not guess E cores");
    Check(affinity::SelectEfficiencyMask({{0, 1, 1}, {1, 0, 2}}) == 0,
        "Multiple processor groups rejected");
    Check(affinity::SelectEfficiencyMask({}) == 0, "Empty topology rejected");
    if (sizeof(DWORD_PTR) == 8) {
        const auto highBit = static_cast<DWORD_PTR>(1ULL << 63);
        Check(affinity::SelectEfficiencyMask({{1, 0, 1}, {0, 0, highBit}}) == highBit,
            "Logical processor 63 is not truncated");
    }
}

void CheckActualProcess() {
    wchar_t executable[32768]{};
    const DWORD length = GetModuleFileNameW(nullptr, executable, 32768);
    Check(length > 0 && length < 32768, "Locate test executable");
    if (!length || length >= 32768) return;
    const std::wstring path(executable);
    const auto filename = path.substr(path.find_last_of(L"\\/") + 1);
    const auto fixture = path.substr(0, path.find_last_of(L"\\/") + 1) + L"blacklist.txt";
    if (GetFileAttributesW(fixture.c_str()) != INVALID_FILE_ATTRIBUTES) {
        Check(false, "Test directory must not contain an existing blacklist.txt");
        return;
    }
    affinity::Policy missing;
    missing.Initialize(Capture);
    Check(!missing.Contains(filename.c_str()), "Missing blacklist disables rules");

    const int count = WideCharToMultiByte(CP_UTF8, 0, filename.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string bytes(count, '\0');
    WideCharToMultiByte(CP_UTF8, 0, filename.c_str(), -1, bytes.data(), count, nullptr, nullptr);
    bytes.pop_back();
    bytes += "\nwrong-name.exe\n";
    HANDLE file = CreateFileW(fixture.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    Check(file != INVALID_HANDLE_VALUE, "Create isolated blacklist fixture");
    if (file == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    const bool saved = WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr)
        && written == bytes.size();
    CloseHandle(file);
    Check(saved, "Write blacklist fixture");
    logs.clear();
    affinity::Policy policy;
    policy.Initialize(Capture, OnApplied);
    Check(policy.Contains(filename.c_str()), "Load blacklist from exe directory, not working directory");
    const auto replaceFixture = [&](const std::string& contents) {
        HANDLE replacement = CreateFileW(fixture.c_str(), GENERIC_WRITE, 0, nullptr,
            TRUNCATE_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (replacement == INVALID_HANDLE_VALUE) return false;
        DWORD countWritten = 0;
        const bool ok = WriteFile(replacement, contents.data(), static_cast<DWORD>(contents.size()),
            &countWritten, nullptr) && countWritten == contents.size();
        CloseHandle(replacement);
        return ok;
    };
    Check(replaceFixture("replacement.exe\n") && policy.Reload()
        && policy.Contains(L"REPLACEMENT.EXE") && !policy.Contains(filename.c_str()),
        "Reload atomically replaces previous names");
    Check(replaceFixture("*.exe\n") && !policy.Reload() && policy.Contains(L"replacement.exe"),
        "Invalid reload preserves active rules");
    Check(replaceFixture("") && policy.Reload() && !policy.Contains(L"replacement.exe"),
        "Empty reload clears active rules");
    Check(replaceFixture(bytes) && policy.Reload() && policy.Contains(filename.c_str()),
        "Reload restores updated rules without restarting");
    Check(DeleteFileW(fixture.c_str()) != FALSE, "Remove test fixture");
    Check(!policy.Reload() && policy.Contains(filename.c_str()), "Missing file on reload preserves active rules");

    const size_t marker = logs.find(L"E-core affinity mask: 0x");
    const DWORD_PTR expected = marker == std::wstring::npos ? 0
        : static_cast<DWORD_PTR>(wcstoull(logs.c_str() + marker + 22, nullptr, 16));
    std::wstring command = L"\"" + path + L"\" --child";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    const bool started = CreateProcessW(path.c_str(), command.data(), nullptr, nullptr, FALSE,
        CREATE_SUSPENDED | CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process) != FALSE;
    Check(started, "Start owned suspended test process");
    if (!started) return;
    DWORD_PTR original = 0;
    DWORD_PTR system = 0;
    Check(GetProcessAffinityMask(process.hProcess, &original, &system) != FALSE, "Read initial affinity");
    // Use a custom original mask so restoring all CPUs cannot accidentally pass.
    original = system & (~system + 1);
    Check(SetProcessAffinityMask(process.hProcess, original) != FALSE, "Set custom original affinity");
    const unsigned int noticesBeforeSkipped = appliedNotifications;
    Check(!policy.Apply(process.dwProcessId, L"not-listed.exe", Now()), "Unlisted process unchanged");
    Check(!policy.Apply(process.dwProcessId, L"wrong-name.exe", Now()), "Mismatched process identity rejected");
    Check(!policy.Apply(process.dwProcessId, filename.c_str(), 1), "Stale event rejected");
    Check(appliedNotifications == noticesBeforeSkipped, "Skipped processes do not trigger notifications");
    DWORD_PTR afterSkipped = 0;
    Check(GetProcessAffinityMask(process.hProcess, &afterSkipped, &system) && afterSkipped == original,
        "Skipped events preserve original affinity");
    const bool applied = policy.Apply(process.dwProcessId, filename.c_str(), Now());
    if (expected) {
        Check(applied, "Set real E-core affinity on owned process");
        Check(appliedNotifications == noticesBeforeSkipped + 1, "Successful binding triggers a notification");
        DWORD_PTR actual = 0;
        Check(GetProcessAffinityMask(process.hProcess, &actual, &system)
            && actual == (expected & system), "Read back exact E-core affinity mask");
        policy.Apply(process.dwProcessId, filename.c_str(), Now());
        policy.Apply(process.dwProcessId, filename.c_str(), Now());
        Check(appliedNotifications == noticesBeforeSkipped + 1, "Repeated unchanged bindings do not notify again");
        policy.UnloadBlacklist();
        Check(!policy.Contains(filename.c_str()), "Unloading disables blacklist matching");
        Check(GetProcessAffinityMask(process.hProcess, &actual, &system) && actual == original,
            "Repeated application restores the first custom original mask");

        HANDLE fixtureFile = CreateFileW(fixture.c_str(), GENERIC_WRITE, 0, nullptr,
            CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        Check(fixtureFile != INVALID_HANDLE_VALUE, "Create reload lifecycle fixture");
        if (fixtureFile != INVALID_HANDLE_VALUE) {
            CloseHandle(fixtureFile);
            Check(replaceFixture(bytes) && policy.Reload()
                && GetProcessAffinityMask(process.hProcess, &actual, &system)
                && actual == (expected & system), "Reload scans and binds an existing process");
            Check(replaceFixture("*.exe\n") && !policy.Reload()
                && GetProcessAffinityMask(process.hProcess, &actual, &system)
                && actual == (expected & system), "Failed reload reapplies old rules to running processes");
            Check(replaceFixture("") && policy.Reload()
                && GetProcessAffinityMask(process.hProcess, &actual, &system)
                && actual == original, "Removing a name restores the running process");
            Check(replaceFixture(bytes), "Prepare startup scan fixture");
            {
                affinity::Policy startupPolicy;
                const size_t logStart = logs.size();
                startupPolicy.Initialize(Capture);
                const auto loadLog = logs.substr(logStart);
                const auto scan = loadLog.find(L"Scanning running processes");
                Check(scan != std::wstring::npos
                    && loadLog.find(L"Scanning running processes", scan + 1) == std::wstring::npos,
                    "Loading the blacklist performs exactly one process scan");
                Check(GetProcessAffinityMask(process.hProcess, &actual, &system)
                    && actual == (expected & system), "Startup scans and binds an existing process");
            }
            Check(GetProcessAffinityMask(process.hProcess, &actual, &system) && actual == original,
                "Policy shutdown restores the exact original affinity");
            Check(DeleteFileW(fixture.c_str()) != FALSE, "Remove reload lifecycle fixture");
        }
    } else {
        Check(!applied, "Unsupported topology does not change affinity");
        printf("SKIP: Hardware does not expose a supported heterogeneous topology.\n");
    }
    Check(ResumeThread(process.hThread) != static_cast<DWORD>(-1), "Resume test child");
    Check(WaitForSingleObject(process.hProcess, 5000) == WAIT_OBJECT_0, "Test child exits normally");
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
}
} // namespace

int main(int argc, char**) {
    if (argc > 1) return 0;
    CheckParser();
    CheckTopology();
    CheckActualProcess();
    printf("Failures: %d\n", failures);
    return failures ? 1 : 0;
}
