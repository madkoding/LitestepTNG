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

    // Whitespace separates tokens; "..." and '...' group (quotes removed), like Split-RcLine.
    std::vector<std::wstring> SplitWhitespace(const std::wstring& line) {
        std::vector<std::wstring> tokens;
        std::wstring current;
        wchar_t quote = 0;
        bool inToken = false;
        for (wchar_t c : line) {
            if (quote != 0) {
                if (c == quote) {
                    quote = 0;
                } else {
                    current += c;
                }
                continue;
            }
            if (c == L'"' || c == L'\'') {
                quote = c;
                inToken = true;
                continue;
            }
            if (c == L' ' || c == L'\t' || c == L'\r' || c == L'\n') {
                if (inToken) {
                    tokens.push_back(current);
                    current.clear();
                    inToken = false;
                }
                continue;
            }
            current += c;
            inToken = true;
        }
        if (inToken) {
            tokens.push_back(current);
        }
        return tokens;
    }

    unsigned ReadU16(const unsigned char* p) {
        return static_cast<unsigned>(p[0]) | (static_cast<unsigned>(p[1]) << 8);
    }

    unsigned long ReadU32(const unsigned char* p) {
        return static_cast<unsigned long>(ReadU16(p)) |
            (static_cast<unsigned long>(ReadU16(p + 2)) << 16);
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
        if (from.empty() || tokens[2].empty()) {
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

    std::wstring ExpandLocation(const std::wstring& location, const std::wstring& appPath) {
        if (location.empty() || location.find_first_of(L"\\/:$") != std::wstring::npos) {
            return location;
        }
        return EndsWithDll(location) ? appPath + location : appPath + location + kDll;
    }

    Resolution ResolveNet(const std::wstring& location, const AliasMap& map,
        const std::wstring& appPath, const std::function<bool(const std::wstring&)>& exists) {
        Resolution r = Resolve(location, map, appPath, exists);
        if (r.outcome != Outcome::Unchanged) {
            return r;
        }
        const std::wstring path = ExpandLocation(location, appPath);
        return Resolution{ path, exists(path) ? Outcome::Unchanged : Outcome::Missing };
    }

    Architecture ReadArchitecture(const unsigned char* data, size_t size) {
        if (data == nullptr || size < 0x40 || data[0] != 'M' || data[1] != 'Z') {
            return Architecture::Unknown;
        }
        const unsigned long pe = ReadU32(data + 0x3C);
        // "PE\0\0" (4) + IMAGE_FILE_HEADER (20); Machine is its first field.
        if (pe > size || size - pe < 4 + 20 || data[pe] != 'P' || data[pe + 1] != 'E' ||
            data[pe + 2] != 0 || data[pe + 3] != 0) {
            return Architecture::Unknown;
        }
        const unsigned machine = ReadU16(data + pe + 4);
        if (size - pe >= 4 + 20 + 2) {
            const unsigned magic = ReadU16(data + pe + 24);
            if (magic == 0x10b) {
                return Architecture::Bits32;
            }
            if (magic == 0x20b) {
                return Architecture::Bits64;
            }
            return Architecture::Unknown;
        }
        return machine == 0x14c ? Architecture::Bits32 : Architecture::Unknown;
    }
}
