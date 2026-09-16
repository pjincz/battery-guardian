#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "affinity_policy.h"
#include <algorithm>
#include <cstddef>
#include <cstdio>

namespace affinity {
namespace {
class Handle {
public:
    explicit Handle(HANDLE value) : value_(value) {}
    ~Handle() { if (value_ && value_ != INVALID_HANDLE_VALUE) CloseHandle(value_); }
    HANDLE Get() const { return value_; }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
private:
    HANDLE value_;
};

void ReportError(Logger log, const wchar_t* operation, DWORD code) {
    log((std::wstring(operation) + L" failed (Win32 error " + std::to_wstring(code) + L").").c_str());
}

bool ReadBlacklist(std::string& bytes, Logger log) {
    std::wstring path(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (!length || length >= path.size()) {
        ReportError(log, L"GetModuleFileName", length ? ERROR_INSUFFICIENT_BUFFER : GetLastError());
        return false;
    }
    path.resize(length);
    path.resize(path.find_last_of(L"\\/") + 1);
    path += L"blacklist.txt";
    log((L"Loading blacklist: " + path).c_str());
    Handle file(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (file.Get() == INVALID_HANDLE_VALUE) {
        ReportError(log, L"Open blacklist.txt", GetLastError());
        return false;
    }
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file.Get(), &size)) {
        ReportError(log, L"Get blacklist size", GetLastError());
        return false;
    }
    if (size.QuadPart < 0 || size.QuadPart > 1024 * 1024) {
        log(L"blacklist.txt exceeds the 1 MiB size limit.");
        return false;
    }
    bytes.resize(static_cast<size_t>(size.QuadPart));
    DWORD read = 0;
    if (!ReadFile(file.Get(), bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr)) {
        ReportError(log, L"Read blacklist.txt", GetLastError());
        return false;
    }
    if (read != bytes.size()) {
        log(L"blacklist.txt changed while it was being read. Restart to retry.");
        return false;
    }
    return true;
}

DWORD_PTR DetectEfficiencyMask(Logger log) {
    // A hard process affinity mask addresses one group. Never truncate group IDs.
    if (GetActiveProcessorGroupCount() != 1 || sizeof(DWORD_PTR) != 8) {
        log(L"E-core binding requires a 64-bit build and a single processor group.");
        return 0;
    }
    DWORD size = 0;
    GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr, &size);
    if (!size || GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
        ReportError(log, L"Get CPU topology size", GetLastError());
        return 0;
    }
    std::vector<BYTE> buffer(size);
    if (!GetLogicalProcessorInformationEx(RelationProcessorCore,
        reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(buffer.data()), &size)) {
        ReportError(log, L"Get CPU topology", GetLastError());
        return 0;
    }
    std::vector<Core> cores;
    size_t offset = 0;
    while (offset < size) {
        constexpr size_t minimum = offsetof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX, Processor)
            + offsetof(PROCESSOR_RELATIONSHIP, GroupMask) + sizeof(GROUP_AFFINITY);
        if (size - offset < minimum) return 0;
        const auto* info = reinterpret_cast<const SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(buffer.data() + offset);
        if (info->Size < minimum || info->Size > size - offset
            || info->Relationship != RelationProcessorCore || info->Processor.GroupCount != 1) return 0;
        const auto& core = info->Processor;
        cores.push_back({core.EfficiencyClass, core.GroupMask[0].Group, core.GroupMask[0].Mask});
        offset += info->Size;
    }
    return SelectEfficiencyMask(cores);
}
} // namespace

bool Matches(const std::vector<std::wstring>& names, const wchar_t* name) {
    for (const auto& entry : names) {
        if (CompareStringOrdinal(entry.c_str(), -1, name, -1, TRUE) == CSTR_EQUAL) return true;
    }
    return false;
}

bool ParseBlacklist(const std::string& bytes, std::vector<std::wstring>& names,
    std::wstring& error) {
    names.clear();
    error.clear();
    if (bytes.empty()) return true;
    const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
        bytes.data(), static_cast<int>(bytes.size()), nullptr, 0);
    if (!count) {
        error = L"blacklist.txt must be UTF-8 text.";
        return false;
    }
    std::wstring text(count, L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(),
        static_cast<int>(bytes.size()), text.data(), count);
    if (text.front() == 0xFEFF) text.erase(0, 1);
    std::vector<std::wstring> parsed;
    size_t start = 0;
    size_t lineNumber = 0;
    while (start < text.size()) {
        ++lineNumber;
        const size_t end = text.find(L'\n', start);
        std::wstring line = text.substr(start, end == std::wstring::npos ? end : end - start);
        start = end == std::wstring::npos ? text.size() : end + 1;
        const size_t first = line.find_first_not_of(L" \t\r");
        if (first == std::wstring::npos || line[first] == L'#') continue;
        line = line.substr(first, line.find_last_not_of(L" \t\r") - first + 1);
        if (line.find_first_of(L"\\/:*?\"<>|") != std::wstring::npos
            || line.find_first_of(std::wstring(L"\0\t\r", 3)) != std::wstring::npos
            || line.size() <= 4
            || CompareStringOrdinal(line.c_str() + line.size() - 4, 4, L".exe", 4, TRUE) != CSTR_EQUAL) {
            error = L"Invalid exe filename on blacklist line " + std::to_wstring(lineNumber) + L".";
            return false;
        }
        if (!Matches(parsed, line.c_str())) parsed.push_back(line);
    }
    names = std::move(parsed);
    return true;
}

DWORD_PTR SelectEfficiencyMask(const std::vector<Core>& cores) {
    if (cores.empty()) return 0;
    BYTE lowest = 255;
    BYTE highest = 0;
    for (const auto& core : cores) {
        if (core.group != 0 || !core.mask) return 0;
        lowest = std::min(lowest, core.efficiencyClass);
        highest = std::max(highest, core.efficiencyClass);
    }
    // Windows defines lower classes as more efficient. Equal classes are ambiguous.
    if (lowest == highest) return 0;
    DWORD_PTR mask = 0;
    for (const auto& core : cores) {
        if (core.efficiencyClass == lowest) mask |= core.mask;
    }
    return mask;
}

void Policy::Initialize(Logger logger) {
    log_ = logger;
    names_.clear();
    mask_ = 0;
    if (!Reload()) log_(L"No initial blacklist loaded. No executable names are active.");
    mask_ = DetectEfficiencyMask(log_);
    if (!mask_) {
        log_(L"No unambiguous E-core mask available. Affinity changes are disabled.");
        return;
    }
    wchar_t message[128]{};
    swprintf_s(message, L"E-core affinity mask: 0x%llX (lowest EfficiencyClass).",
        static_cast<unsigned long long>(mask_));
    log_(message);
}

bool Policy::Reload() {
    std::string bytes;
    std::wstring error;
    std::vector<std::wstring> replacement;
    if (!ReadBlacklist(bytes, log_) || !ParseBlacklist(bytes, replacement, error)) {
        if (!error.empty()) log_(error.c_str());
        log_(L"Blacklist reload failed. Previous rules are unchanged.");
        return false;
    }
    names_.swap(replacement);
    log_((L"Blacklist loaded: " + std::to_wstring(names_.size()) + L" executable name(s).").c_str());
    return true;
}

bool Policy::Contains(const wchar_t* name) const {
    return Matches(names_, name);
}

bool Policy::Apply(DWORD processId, const wchar_t* eventName, ULONGLONG eventTime) const {
    if (!Contains(eventName)) return false;
    const std::wstring label = std::wstring(eventName) + L" (PID " + std::to_wstring(processId) + L")";
    if (!mask_) {
        log_((label + L": skipped; E-core binding is unavailable.").c_str());
        return false;
    }
    Handle process(OpenProcess(PROCESS_SET_INFORMATION | PROCESS_QUERY_LIMITED_INFORMATION,
        FALSE, processId));
    if (!process.Get()) {
        ReportError(log_, (L"Open " + label).c_str(), GetLastError());
        return false;
    }
    // Check identity after opening the handle to avoid acting on a recycled PID.
    std::wstring image(32768, L'\0');
    DWORD length = static_cast<DWORD>(image.size());
    FILETIME created{}, exited{}, kernel{}, user{};
    if (!QueryFullProcessImageNameW(process.Get(), 0, image.data(), &length)
        || !GetProcessTimes(process.Get(), &created, &exited, &kernel, &user)) {
        ReportError(log_, (L"Verify " + label).c_str(), GetLastError());
        return false;
    }
    image.resize(length);
    const std::wstring actualName = image.substr(image.find_last_of(L"\\/") + 1);
    const ULONGLONG creationTime = (static_cast<ULONGLONG>(created.dwHighDateTime) << 32) | created.dwLowDateTime;
    if (!eventTime || creationTime > eventTime
        || CompareStringOrdinal(actualName.c_str(), -1, eventName, -1, TRUE) != CSTR_EQUAL) {
        log_((label + L": skipped; process identity does not match the start event.").c_str());
        return false;
    }
    DWORD_PTR previous = 0;
    DWORD_PTR system = 0;
    if (!GetProcessAffinityMask(process.Get(), &previous, &system)) {
        ReportError(log_, (L"Get affinity for " + label).c_str(), GetLastError());
        return false;
    }
    const DWORD_PTR target = mask_ & system;
    if (!target) {
        log_((label + L": skipped; no eligible E cores.").c_str());
        return false;
    }
    if (!SetProcessAffinityMask(process.Get(), target)) {
        ReportError(log_, (L"Set affinity for " + label).c_str(), GetLastError());
        return false;
    }
    wchar_t maskText[32]{};
    swprintf_s(maskText, L"0x%llX", static_cast<unsigned long long>(target));
    log_((label + L": bound to E cores; affinity mask " + maskText + L".").c_str());
    return true;
}
} // namespace affinity
