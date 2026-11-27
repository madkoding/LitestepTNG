//-------------------------------------------------------------------------------------------------
// /litestep/ModuleResolver.h
// The LiteStepTNG Project
//
// Replaces the module names of classic themes (label-1.99, popup2...) with the repo's nModules,
// following the "*ModuleAlias <name> <Target>" lines of config\module-map.rc.
// Standard library only (no Windows headers) so tools\tests\module_resolver_test.cpp can build
// it with g++ / zig c++. Case folding is ASCII-only, as module names are.
//-------------------------------------------------------------------------------------------------
#pragma once

#include <functional>
#include <map>
#include <string>

namespace ModuleResolver {
    // File name only (everything up to the last \, / or $ is a directory), lower case (ASCII),
    // no ".dll", and no trailing '-' + text that starts with a digit:
    //   "$LiteStepDir$Popup.dll" -> "popup", "textedit-2.4l-13" -> "textedit", "ls-mute" -> "ls-mute".
    // Pinned by tools\tests\fixtures\module-names.txt (same rule as normalise() in
    // tools\tests\test_module_map.py).
    std::wstring NormalizeName(const std::wstring& location);

    struct Alias {
        std::wstring from;  // normalised
        std::wstring to;    // as written: the DLL name without ".dll"
    };

    // Parses "*ModuleAlias <name> <Target>" (key case-insensitive, exactly three tokens separated
    // by spaces or tabs; "..." or '...' groups a token and one pair of outer quotes is removed,
    // as Split-RcLine in tools\theme-plan.ps1 does). Returns false for anything else.
    bool ParseAliasLine(const std::wstring& line, Alias& out);

    class AliasMap {
    public:
        // The first alias added for a name wins (personal.rc is read before the map).
        void Add(const Alias& alias);
        // The target for a normalised name, or nullptr.
        const std::wstring* Find(const std::wstring& normalizedName) const;

    private:
        std::map<std::wstring, std::wstring> mAliases;
    };

    enum class Outcome {
        Unchanged,      // no alias, or the location already is the target (see Resolve)
        Mapped,         // location = <appPath><Target>.dll
        KeptOriginal,   // the target is not installed; location = the original module's path
        Missing         // neither the target nor the original exists
    };

    struct Resolution {
        std::wstring location;
        Outcome outcome;
    };

    // location: what the theme's LoadModule line names, possibly already expanded
    // ("C:\run\Popup.dll"). appPath: litestep's folder, ending with '\'.
    // Order: normalise; no alias -> Unchanged; the location already is the target -> Unchanged;
    // <appPath><Target>.dll exists -> Mapped; the original exists -> KeptOriginal; else Missing.
    // "Already is the target": its file name without ".dll" equals the target (ASCII case-
    // insensitive) and it has no folder, the folder appPath, or a folder still written with a
    // $Variable$ ("$LiteStepDir$Popup.dll"). So "*ModuleAlias popup2 popup2" keeps popup2.dll,
    // while "label-1.99" (name "label", target "Label") is still mapped: comparing the target
    // with the normalised name instead would make every "label Label" line a no-op.
    // A bare name (no \, / , : or $ and not ending in ".dll", e.g. "label-1.99") stands for
    // <appPath><name>.dll when checking whether the original exists and in KeptOriginal.
    // Unchanged leaves the location exactly as written (a bare name is NOT expanded), so the
    // existing loader gets what it got before this resolver existed. Missing keeps the
    // original location (expanded like KeptOriginal) for error messages.
    Resolution Resolve(const std::wstring& location, const AliasMap& map,
        const std::wstring& appPath, const std::function<bool(const std::wstring&)>& exists);

    // The file a location stands for: a name with no folder (no \, /, : or $) is
    // <appPath><name>, plus ".dll" unless it already ends with it ("label-1.99" ->
    // <appPath>label-1.99.dll; LoadLibrary would not add ".dll" to a name with a dot). Anything
    // else is returned as written. Used as the key that finds two lines naming the same DLL.
    std::wstring ExpandLocation(const std::wstring& location, const std::wstring& appPath);

    // Resolve for *NetLoadModule / *NetInstallModule lines, which name a module the way a 2002
    // theme did and must never reach the loader's "Could not locate module" box: when Resolve
    // says Unchanged, the location is expanded (ExpandLocation) and the outcome becomes Missing
    // if that file does not exist. The other outcomes are Resolve's. LoadModule lines keep
    // using Resolve, which leaves an Unchanged location exactly as written.
    Resolution ResolveNet(const std::wstring& location, const AliasMap& map,
        const std::wstring& appPath, const std::function<bool(const std::wstring&)>& exists);

    enum class Architecture {
        Unknown,    // too short, not a PE file, or an optional header this code does not know
        Bits32,     // optional header magic 0x10b (or, without one, machine i386)
        Bits64      // optional header magic 0x20b
    };

    // Reads the architecture from the first bytes of a DLL: MZ, e_lfanew (offset 0x3C),
    // "PE\0\0", IMAGE_FILE_HEADER (Machine), then the optional header's magic.
    Architecture ReadArchitecture(const unsigned char* data, size_t size);
}
