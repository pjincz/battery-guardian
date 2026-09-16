#pragma once

#include <windows.h>
#include <string>
#include <vector>

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
    void Initialize(Logger logger);
    bool Contains(const wchar_t* name) const;
    bool Apply(DWORD processId, const wchar_t* eventName, ULONGLONG eventTime) const;

private:
    std::vector<std::wstring> names_;
    DWORD_PTR mask_ = 0;
    Logger log_ = nullptr;
};
} // namespace affinity
