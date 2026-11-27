//-------------------------------------------------------------------------------------------------
// /litestep/ModuleResolver.cpp
// The LiteStepTNG Project
//
// See ModuleResolver.h. Standard library only.
//-------------------------------------------------------------------------------------------------
#include "ModuleResolver.h"

#include <vector>

namespace {
    const wchar_t kDll[] = L".dll";
    const size_t kDllLength = 4;

    wchar_t LowerAscii(wchar_t c) {
        return (c >= L'A' && c <= L'Z') ? static_cast<wchar_t>(c - L'A' + L'a') : c;
    }

    std::wstring ToLowerAscii(const std::wstring& s) {
        std::wstring out(s);
        for (wchar_t& c : out) {
            c = LowerAscii(c);
        }
        return out;
    }

    bool EqualsNoCase(const std::wstring& a, const std::wstring& b) {
        return ToLowerAscii(a) == ToLowerAscii(b);
    }

    bool IsDigit(wchar_t c) {
        return c >= L'0' && c <= L'9';
    }

    bool EndsWithDll(const std::wstring& s) {
        return s.size() >= kDllLength &&
            ToLowerAscii(s.substr(s.size() - kDllLength)) == kDll;
    }

    bool IsBareName(const std::wstring& location) {
        return location.find_first_of(L"\\/:$") == std::wstring::npos && !EndsWithDll(location);
    }

    // True when location already names <appPath><target>.dll: same file name (case-insensitive)
    // and no folder, appPath itself, or a folder still written as a $Variable$ (unknown here,
    // so it is left as written). A classic module with that name elsewhere is not the target.
    bool IsTargetItself(const std::wstring& location, const std::wstring& target,
        const std::wstring& appPath) {
        size_t slash = location.find_last_of(L"\\/$");
        std::wstring folder = slash == std::wstring::npos ? L"" : location.substr(0, slash + 1);
        std::wstring file = slash == std::wstring::npos ? location : location.substr(slash + 1);
        if (EndsWithDll(file)) {
            file.erase(file.size() - kDllLength);
        }
        if (!EqualsNoCase(file, target)) {
            return false;
        }
        return folder.empty() || folder.find(L'$') != std::wstring::npos ||
            EqualsNoCase(folder, appPath);
    }

    std::vector<std::wstring> SplitWhitespace(const std::wstring& line) {
        std::vector<std::wstring> tokens;
        size_t pos = 0;
        while (pos < line.size()) {
            size_t start = line.find_first_not_of(L" \t\r\n", pos);
            if (start == std::wstring::npos) {
                break;
            }
            size_t end = line.find_first_of(L" \t\r\n", start);
            if (end == std::wstring::npos) {
                end = line.size();
            }
            tokens.push_back(line.substr(start, end - start));
            pos = end;
        }
        return tokens;
    }
}

namespace ModuleResolver {
    std::wstring NormalizeName(const std::wstring& location) {
        size_t slash = location.find_last_of(L"\\/$");
        std::wstring name = ToLowerAscii(
            slash == std::wstring::npos ? location : location.substr(slash + 1));
        if (EndsWithDll(name)) {
            name.erase(name.size() - kDllLength);
        }
        for (size_t i = 0; i + 1 < name.size(); ++i) {
            if (name[i] == L'-' && IsDigit(name[i + 1])) {
                name.erase(i);
                break;
            }
        }
        return name;
    }

    bool ParseAliasLine(const std::wstring& line, Alias& out) {
        std::vector<std::wstring> tokens = SplitWhitespace(line);
        if (tokens.size() != 3 || !EqualsNoCase(tokens[0], L"*ModuleAlias")) {
            return false;
        }
        std::wstring from = NormalizeName(tokens[1]);
        if (from.empty()) {
            return false;
        }
        out.from = from;
        out.to = tokens[2];
        return true;
    }

    void AliasMap::Add(const Alias& alias) {
        mAliases.insert(std::make_pair(alias.from, alias.to));  // keeps an existing entry
    }

    const std::wstring* AliasMap::Find(const std::wstring& normalizedName) const {
        auto it = mAliases.find(normalizedName);
        return it == mAliases.end() ? nullptr : &it->second;
    }

    Resolution Resolve(const std::wstring& location, const AliasMap& map,
        const std::wstring& appPath, const std::function<bool(const std::wstring&)>& exists) {
        const std::wstring name = NormalizeName(location);
        const std::wstring* to = map.Find(name);
        if (to == nullptr || IsTargetItself(location, *to, appPath)) {
            return Resolution{ location, Outcome::Unchanged };
        }

        const std::wstring target = appPath + *to + kDll;
        if (exists(target)) {
            return Resolution{ target, Outcome::Mapped };
        }

        const std::wstring original =
            IsBareName(location) ? appPath + location + kDll : location;
        if (exists(original)) {
            return Resolution{ original, Outcome::KeptOriginal };
        }
        return Resolution{ original, Outcome::Missing };
    }
}
