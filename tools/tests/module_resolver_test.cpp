// Test of litestep\ModuleResolver (std-only). Built and run by test_module_resolver.py:
//   module_resolver_test <path of fixtures\module-names.txt>
// Exit code = number of failed checks.
#include "ModuleResolver.h"

#include <fstream>
#include <iostream>
#include <set>
#include <string>
#include <vector>

using namespace ModuleResolver;

static int gFailures = 0;

static std::string Narrow(const std::wstring& s) {
    std::string out;
    for (wchar_t c : s) {
        out += (c < 128) ? static_cast<char>(c) : '?';
    }
    return out;
}

static std::wstring Widen(const std::string& s) {
    std::wstring out;
    for (char c : s) {
        out += static_cast<wchar_t>(static_cast<unsigned char>(c));
    }
    return out;
}

static void Check(bool ok, const char* test, const std::string& what) {
    if (!ok) {
        ++gFailures;
        std::cout << "FAIL " << test << ": " << what << "\n";
    }
}

static void CheckEqual(const std::wstring& got, const std::wstring& expected, const char* test,
                       const std::string& what) {
    Check(got == expected, test,
          what + ": got \"" + Narrow(got) + "\", expected \"" + Narrow(expected) + "\"");
}

static const char* OutcomeName(Outcome o) {
    switch (o) {
    case Outcome::Unchanged: return "Unchanged";
    case Outcome::Mapped: return "Mapped";
    case Outcome::KeptOriginal: return "KeptOriginal";
    case Outcome::Missing: return "Missing";
    }
    return "?";
}

static void CheckOutcome(Outcome got, Outcome expected, const char* test) {
    Check(got == expected, test,
          std::string("outcome ") + OutcomeName(got) + ", expected " + OutcomeName(expected));
}

static std::string Trim(const std::string& s) {
    size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

// A file system that holds exactly the given paths (compared exactly).
struct FakeFiles {
    std::set<std::wstring> files;
    std::function<bool(const std::wstring&)> Exists() const {
        const std::set<std::wstring>* f = &files;
        return [f](const std::wstring& path) { return f->count(path) != 0; };
    }
};

static const std::wstring kApp = L"C:\\run\\";

static AliasMap MapOf(std::initializer_list<const wchar_t*> lines) {
    AliasMap map;
    for (const wchar_t* line : lines) {
        Alias a;
        if (ParseAliasLine(line, a)) map.Add(a);
    }
    return map;
}

static void test_normalize_fixture(const char* fixturePath) {
    const char* T = "test_normalize_fixture";
    std::ifstream in(fixturePath);
    Check(static_cast<bool>(in), T, std::string("cannot open ") + fixturePath);
    std::string line;
    int cases = 0;
    while (std::getline(in, line)) {
        std::string t = Trim(line);
        if (t.empty() || t[0] == ';') continue;
        size_t arrow = t.find(" -> ");
        Check(arrow != std::string::npos, T, "malformed line \"" + t + "\"");
        if (arrow == std::string::npos) continue;
        std::string given = Trim(t.substr(0, arrow));
        std::string expected = Trim(t.substr(arrow + 4));
        CheckEqual(NormalizeName(Widen(given)), Widen(expected), T, "NormalizeName(" + given + ")");
        ++cases;
    }
    Check(cases > 0, T, "no cases in the fixture");
}

static void test_parse_alias_line() {
    const char* T = "test_parse_alias_line";
    Alias a;
    Check(ParseAliasLine(L"*ModuleAlias label Label", a), T, "valid line rejected");
    CheckEqual(a.from, L"label", T, "from");
    CheckEqual(a.to, L"Label", T, "to");

    Alias b;
    Check(ParseAliasLine(L"  *modulealias   Label-1.99.dll\tTaskBar  ", b), T,
          "key is case-insensitive and whitespace is free");
    CheckEqual(b.from, L"label", T, "from is normalised");
    CheckEqual(b.to, L"TaskBar", T, "to is kept as written");

    Alias c;
    Check(!ParseAliasLine(L"*ModuleAliases label Label", c), T, "wrong key accepted");
    Check(!ParseAliasLine(L"*NetLoadModule label Label", c), T, "other key accepted");
    Check(!ParseAliasLine(L"*ModuleAlias label", c), T, "missing target accepted");
    Check(!ParseAliasLine(L"*ModuleAlias", c), T, "missing name accepted");
    Check(!ParseAliasLine(L"", c), T, "empty line accepted");
    Check(!ParseAliasLine(L"*ModuleAlias a b c", c), T, "extra token accepted");
}

static void test_first_alias_wins() {
    const char* T = "test_first_alias_wins";
    AliasMap map = MapOf({L"*ModuleAlias popup2 popup2", L"*ModuleAlias popup2 Popup"});
    const std::wstring* to = map.Find(L"popup2");
    Check(to != nullptr, T, "popup2 not found");
    if (to) CheckEqual(*to, L"popup2", T, "target");
    Check(map.Find(L"label") == nullptr, T, "unknown name found");
}

static void test_maps_to_nmodule() {
    const char* T = "test_maps_to_nmodule";
    AliasMap map = MapOf({L"*ModuleAlias label Label"});
    FakeFiles fs;
    fs.files.insert(kApp + L"Label.dll");
    fs.files.insert(kApp + L"label-1.99.dll");
    Resolution r = Resolve(L"label-1.99", map, kApp, fs.Exists());
    CheckOutcome(r.outcome, Outcome::Mapped, T);
    CheckEqual(r.location, kApp + L"Label.dll", T, "location");

    Resolution full = Resolve(L"C:\\themes\\x\\label-1.99.dll", map, kApp, fs.Exists());
    CheckOutcome(full.outcome, Outcome::Mapped, T);
    CheckEqual(full.location, kApp + L"Label.dll", T, "location of a full path");
}

static void test_falls_back_to_original() {
    const char* T = "test_falls_back_to_original";
    AliasMap map = MapOf({L"*ModuleAlias label Label"});
    FakeFiles fs;
    fs.files.insert(kApp + L"label-1.99.dll");
    Resolution r = Resolve(L"label-1.99", map, kApp, fs.Exists());
    CheckOutcome(r.outcome, Outcome::KeptOriginal, T);
    CheckEqual(r.location, kApp + L"label-1.99.dll", T, "location");

    FakeFiles fs2;
    fs2.files.insert(L"C:\\themes\\x\\label-1.99.dll");
    Resolution full = Resolve(L"C:\\themes\\x\\label-1.99.dll", map, kApp, fs2.Exists());
    CheckOutcome(full.outcome, Outcome::KeptOriginal, T);
    CheckEqual(full.location, L"C:\\themes\\x\\label-1.99.dll", T, "location of a full path");
}

static void test_unknown_bare_name_is_missing() {
    const char* T = "test_unknown_bare_name_is_missing";
    AliasMap map = MapOf({L"*ModuleAlias geekamp Label"});
    FakeFiles fs;
    Resolution r = Resolve(L"geekamp-1.9.7", map, kApp, fs.Exists());
    CheckOutcome(r.outcome, Outcome::Missing, T);

    // A name with no alias is never touched, whether it exists or not.
    AliasMap empty;
    Resolution u = Resolve(L"geekamp-1.9.7", empty, kApp, fs.Exists());
    CheckOutcome(u.outcome, Outcome::Unchanged, T);
    CheckEqual(u.location, L"geekamp-1.9.7", T, "location without alias");
}

static void test_self_alias_keeps_original() {
    const char* T = "test_self_alias_keeps_original";
    AliasMap map = MapOf({L"*ModuleAlias popup2 popup2"});
    FakeFiles fs;
    fs.files.insert(kApp + L"popup2.dll");
    Resolution r = Resolve(L"popup2.dll", map, kApp, fs.Exists());
    CheckOutcome(r.outcome, Outcome::Unchanged, T);
    CheckEqual(r.location, L"popup2.dll", T, "location");
}

static void test_already_nmodule_unchanged() {
    const char* T = "test_already_nmodule_unchanged";
    AliasMap map = MapOf({L"*ModuleAlias popup Popup"});
    FakeFiles fs;
    fs.files.insert(kApp + L"Popup.dll");
    Resolution r = Resolve(L"$LiteStepDir$Popup.dll", map, kApp, fs.Exists());
    CheckOutcome(r.outcome, Outcome::Unchanged, T);
    CheckEqual(r.location, L"$LiteStepDir$Popup.dll", T, "location");

    Resolution e = Resolve(L"C:\\run\\Popup.dll", map, kApp, fs.Exists());
    CheckOutcome(e.outcome, Outcome::Unchanged, T);
    CheckEqual(e.location, L"C:\\run\\Popup.dll", T, "expanded location");

    Resolution lower = Resolve(L"c:\\RUN\\popup.dll", map, kApp, fs.Exists());
    CheckOutcome(lower.outcome, Outcome::Unchanged, T);

    // Same file name, other folder: a classic theme's own popup.dll, so it is replaced.
    Resolution other = Resolve(L"C:\\run\\modules\\popup.dll", map, kApp, fs.Exists());
    CheckOutcome(other.outcome, Outcome::Mapped, T);
    CheckEqual(other.location, kApp + L"Popup.dll", T, "location of a classic popup.dll");
}

static void test_parse_alias_line_quotes() {
    const char* T = "test_parse_alias_line_quotes";
    Alias a;
    Check(ParseAliasLine(L"*ModuleAlias \"label-1.99\" \"Label\"", a), T, "quoted line rejected");
    CheckEqual(a.from, L"label", T, "quoted from");
    CheckEqual(a.to, L"Label", T, "quoted to");

    Alias b;
    Check(ParseAliasLine(L"*ModuleAlias 'popup2' 'My Popup'", b), T, "single quotes rejected");
    CheckEqual(b.from, L"popup2", T, "single-quoted from");
    CheckEqual(b.to, L"My Popup", T, "a quoted target keeps its space");

    Alias c;
    Check(!ParseAliasLine(L"*ModuleAlias label \"\"", c), T, "empty target accepted");
}

static void test_net_unaliased_bare_name() {
    const char* T = "test_net_unaliased_bare_name";
    AliasMap empty;
    FakeFiles none;
    Resolution r = ResolveNet(L"geekamp-1.9.7", empty, kApp, none.Exists());
    CheckOutcome(r.outcome, Outcome::Missing, T);
    CheckEqual(r.location, kApp + L"geekamp-1.9.7.dll", T, "location of an absent module");

    FakeFiles fs;
    fs.files.insert(kApp + L"lsxcommand-1.9.9.dll");
    Resolution p = ResolveNet(L"lsxcommand-1.9.9", empty, kApp, fs.Exists());
    CheckOutcome(p.outcome, Outcome::Unchanged, T);
    CheckEqual(p.location, kApp + L"lsxcommand-1.9.9.dll", T, "a present module is expanded");

    // A full path that does not exist is skipped too; one that exists is kept as written.
    Resolution full = ResolveNet(L"C:\\x\\geekamp.dll", empty, kApp, none.Exists());
    CheckOutcome(full.outcome, Outcome::Missing, T);
    FakeFiles fx;
    fx.files.insert(L"C:\\x\\geekamp.dll");
    Resolution kept = ResolveNet(L"C:\\x\\geekamp.dll", empty, kApp, fx.Exists());
    CheckOutcome(kept.outcome, Outcome::Unchanged, T);
    CheckEqual(kept.location, L"C:\\x\\geekamp.dll", T, "full path kept");

    // Aliased names resolve as with Resolve.
    AliasMap map = MapOf({L"*ModuleAlias label Label"});
    FakeFiles lf;
    lf.files.insert(kApp + L"Label.dll");
    Resolution m = ResolveNet(L"label-1.99", map, kApp, lf.Exists());
    CheckOutcome(m.outcome, Outcome::Mapped, T);
    CheckEqual(m.location, kApp + L"Label.dll", T, "mapped location");

    // A self alias still loads the original, expanded.
    AliasMap self = MapOf({L"*ModuleAlias popup2 popup2"});
    FakeFiles pf;
    pf.files.insert(kApp + L"popup2.dll");
    Resolution s = ResolveNet(L"popup2", self, kApp, pf.Exists());
    CheckOutcome(s.outcome, Outcome::Unchanged, T);
    CheckEqual(s.location, kApp + L"popup2.dll", T, "self alias location");

    // LoadModule lines (Resolve) keep the location as written.
    Resolution lm = Resolve(L"geekamp-1.9.7", empty, kApp, none.Exists());
    CheckOutcome(lm.outcome, Outcome::Unchanged, T);
    CheckEqual(lm.location, L"geekamp-1.9.7", T, "Resolve leaves a bare name alone");
}

static void test_expand_location() {
    const char* T = "test_expand_location";
    CheckEqual(ExpandLocation(L"Label", kApp), kApp + L"Label.dll", T, "bare");
    CheckEqual(ExpandLocation(L"label-1.99", kApp), kApp + L"label-1.99.dll", T, "dotted bare");
    CheckEqual(ExpandLocation(L"Label.dll", kApp), kApp + L"Label.dll", T, "file name");
    CheckEqual(ExpandLocation(L"C:\\run\\Label.dll", kApp), L"C:\\run\\Label.dll", T, "path");
    CheckEqual(ExpandLocation(L"$LiteStepDir$Label.dll", kApp), L"$LiteStepDir$Label.dll", T,
               "variable");
}

static std::vector<unsigned char> MinimalPe(unsigned machine, unsigned magic, size_t size) {
    std::vector<unsigned char> b(size, 0);
    b[0] = 'M'; b[1] = 'Z';
    b[0x3C] = 0x40;
    b[0x40] = 'P'; b[0x41] = 'E';
    b[0x44] = static_cast<unsigned char>(machine & 0xFF);
    b[0x45] = static_cast<unsigned char>(machine >> 8);
    if (size >= 0x5A) {
        b[0x58] = static_cast<unsigned char>(magic & 0xFF);
        b[0x59] = static_cast<unsigned char>(magic >> 8);
    }
    return b;
}

static void test_read_architecture() {
    const char* T = "test_read_architecture";
    std::vector<unsigned char> x86 = MinimalPe(0x14c, 0x10b, 0x80);
    Check(ReadArchitecture(x86.data(), x86.size()) == Architecture::Bits32, T, "PE32");
    std::vector<unsigned char> x64 = MinimalPe(0x8664, 0x20b, 0x80);
    Check(ReadArchitecture(x64.data(), x64.size()) == Architecture::Bits64, T, "PE32+");
    std::vector<unsigned char> noMagic = MinimalPe(0x14c, 0, 0x58);
    Check(ReadArchitecture(noMagic.data(), noMagic.size()) == Architecture::Bits32, T,
          "i386 machine without an optional header");
    Check(ReadArchitecture(nullptr, 0) == Architecture::Unknown, T, "empty");
    std::vector<unsigned char> shortFile(0x20, 0);
    shortFile[0] = 'M'; shortFile[1] = 'Z';
    Check(ReadArchitecture(shortFile.data(), shortFile.size()) == Architecture::Unknown, T,
          "too short");
    std::vector<unsigned char> farPe = MinimalPe(0x14c, 0x10b, 0x80);
    farPe[0x3C] = 0xF0; farPe[0x3D] = 0xFF;  // e_lfanew past the end
    Check(ReadArchitecture(farPe.data(), farPe.size()) == Architecture::Unknown, T,
          "e_lfanew past the end");
    std::vector<unsigned char> notPe = MinimalPe(0x14c, 0x10b, 0x80);
    notPe[0x40] = 'X';
    Check(ReadArchitecture(notPe.data(), notPe.size()) == Architecture::Unknown, T, "no PE sig");
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cout << "usage: module_resolver_test <module-names.txt>\n";
        return 100;
    }
    test_normalize_fixture(argv[1]);
    test_parse_alias_line();
    test_first_alias_wins();
    test_maps_to_nmodule();
    test_falls_back_to_original();
    test_unknown_bare_name_is_missing();
    test_self_alias_keeps_original();
    test_already_nmodule_unchanged();
    test_parse_alias_line_quotes();
    test_net_unaliased_bare_name();
    test_expand_location();
    test_read_architecture();
    if (gFailures == 0) std::cout << "OK\n";
    return gFailures;
}
