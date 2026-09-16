#pragma once

#include <windows.h>
#include <string>
#include <vector>
#include <map>

namespace affinity {
using Logger = void (*)(const wchar_t*);

struct Core {
    BYTE efficiencyClass;
    WORD group;
    KAFFINITY mask;
};

bool ParseBlacklist(const std::string& bytes, std::vector<std::wstring>& names,
    std::wstring& error);
bool Matches(const std::vector<std::wstring>& names, const wchar_t* name);
DWORD_PTR SelectEfficiencyMask(const std::vector<Core>& cores);

class Policy {
public:
    Policy() = default;
    ~Policy();
    Policy(const Policy&) = delete;
    Policy& operator=(const Policy&) = delete;
    void Initialize(Logger logger);
    bool LoadBlacklist();
    void UnloadBlacklist();
    bool Reload();
    bool Contains(const wchar_t* name) const;
    bool Apply(DWORD processId, const wchar_t* eventName, ULONGLONG eventTime);

private:
    void ApplyAll();
    void RestoreAll();
    bool loaded_ = false;
    std::vector<std::wstring> names_;
    DWORD_PTR mask_ = 0;
    Logger log_ = nullptr;
    struct OriginalAffinity {
        HANDLE process;
        ULONGLONG creationTime;
        DWORD_PTR mask;
        std::wstring name;
    };
    std::map<DWORD, OriginalAffinity> originals_;
};
} // namespace affinity
