"""rc_effective.py - what a LiteStep theme actually configures.

Reads an rc tree the way litestep does and prints the value every setting would have when a
module reads it, so two versions of a theme can be compared: if the dumps are equal, the theme
behaves the same, however its files are organised.

    python tools/rc_effective.py dump --root <run folder or repo> --theme <Name> [--personal <file>]
    python tools/rc_effective.py diff <before.txt> <after.txt> [--ignore-new <prefix> ...]

Modelled on lsapi/SettingsFileParser.cpp (lines, comments, prefix blocks, include),
lsapi/SettingsManager.cpp (first definition wins, $Var$ expanded when read) and
modules/ModuleKit/Settings.cpp (<Prefix>Group fallbacks, child prefixes along the chain).

Dump lines, sorted:
    S <name> = <value>        a setting as a module reads it (names lower-case)
    L <*key> <n> = <line>     the n-th line of a star key (*Popup, *HotKey...) or of a key read
                              line by line (LoadModule), in file order
    U <var>                   a $var$ that nothing defines
"""
import argparse
import os
import re
import sys

WHITESPACE = ' \t\r\n'
RESERVED = '"\'[];$'
KEYWORDS = ('if', 'else', 'elseif', 'endif')
# Plain keys that litestep reads line by line (every occurrence, in order), not first-wins.
MULTI_KEYS = ('loadmodule', 'netloadmodule')
# <Prefix><Suffix> keys that ModuleKit reads line by line and accumulates along the Group
# chain (Settings::IterateOverCommandLines): click handlers and hidden tray icons. Matched
# case-sensitively, as themes write them, so "...Duration" is not taken for "...On".
CHAIN_SUFFIXES = ('On', 'Hide')
# Keys are cut to MAX_RCCOMMAND - 1 characters by the GetPrefixedRC* helpers.
MAX_NAME = 63


class Unsupported(Exception):
    """A construct this checker does not model (it refuses rather than guesses)."""


def _strip_value(text):
    """Trailing whitespace and a ';' comment outside quotes/brackets go (_StripString)."""
    level = 0
    last_quote = ''
    end = len(text)
    for i, ch in enumerate(text):
        if ch == '[':
            level += 1
        elif ch == ']':
            if level:
                level -= 1
        elif ch in '"\'':
            if last_quote == ch:
                level -= 1
                last_quote = ''
            elif not last_quote:
                level += 1
                last_quote = ch
        elif ch == ';' and level == 0:
            end = i
            break
    return text[:end].strip(WHITESPACE)


def _read_lines(path):
    """litestep opens rc files as UTF-8 ("rt, ccs=UTF-8"); anything else would be garbled
    there, so it is refused here rather than read some other way."""
    with open(path, 'rb') as f:
        raw = f.read()
    try:
        text = raw.decode('utf-8-sig')
    except UnicodeDecodeError as e:
        raise Unsupported('%s is not UTF-8 (%s); litestep would read it garbled' % (path, e))
    # fgets splits on '\n' only (not on U+0085, U+2028... as str.splitlines would).
    return text.replace('\r\n', '\n').split('\n')


def get_token(value):
    """GetTokenW(value, ..., bUseBrackets=FALSE): the first token, outer quotes removed; a quote
    that starts inside a token joins the next token to it."""
    i, n = 0, len(value)
    while i < n and value[i] in WHITESPACE:
        i += 1
    out, quote, started = [], '', False
    while i < n:
        c = value[i]
        if c.isspace() and not quote:
            break
        if c in '"\'':
            if not quote:
                if started:
                    return ''.join(out) + get_token(value[i:])
                quote = c
                i += 1
                continue
            if c == quote:
                break
        started = True
        out.append(c)
        i += 1
    return ''.join(out)


class Tree:
    def __init__(self, root, theme, personal=None, system_language='en'):
        self.root = os.path.abspath(root)
        self.settings = {}      # lower name -> (name, raw value)
        self.order = []         # lower names in definition order
        self.stars = {}         # lower *key -> [raw lines]
        self.chained = {}       # lower <prefix><On|Hide> -> [raw lines], every occurrence
        self.unresolved = set()
        self._including = []
        # Quoted, as theme-select.rc writes ThemeName and litestep stores its own paths: a
        # variable expands to its first token, so an unquoted "LiteStep Enhanced" would be cut.
        self._define('ThemeName', '"%s"' % theme)
        self._define('LiteStepDir', '"%s\\"' % self.root.rstrip('\\/'))
        self._define('USERNAME', '"%s"' % os.environ.get('USERNAME', 'user'))
        # lsapiInit.cpp: the session's display language (a fixed value here, so dumps do not
        # depend on the machine; pass --system-language to try another).
        self._define('SystemLanguage', system_language)
        step = os.path.join(self.root, 'step.rc')
        if personal:
            # step.rc includes personal.rc from the run folder; a given file stands in for it.
            self._personal = os.path.abspath(personal)
        else:
            self._personal = None
        self._parse_file(step)

    # -- parsing -----------------------------------------------------------------------

    def _define(self, name, value):
        low = name.lower()
        if low not in self.settings:
            self.settings[low] = (name, value)
            self.order.append(low)

    def _parse_file(self, path):
        norm = os.path.normcase(os.path.abspath(path))
        if norm in self._including or not os.path.isfile(path):
            return
        self._including.append(norm)
        lines = [l for l in _read_lines(path)]
        prefixes = []           # innermost first
        i = 0
        while i < len(lines):
            line = lines[i].lstrip(WHITESPACE)
            i += 1
            if not line or line[0] == ';':
                continue
            if line[0] == '}':
                if prefixes:
                    prefixes.pop(0)
                continue
            end = 0
            while end < len(line) and line[end] not in WHITESPACE + RESERVED:
                end += 1
            if end == 0 or (end < len(line) and line[end] not in WHITESPACE + ';'):
                continue        # invalid line format; litestep ignores it
            word = line[:end]
            rest = line[end:]

            name = ''
            if prefixes:
                if word.startswith('*'):
                    name = '*'
                    word = word[1:]
                if word.lower() not in KEYWORDS:
                    name += prefixes[0]
                if word == '-':
                    word = ''
            name += word

            # A name followed by a line that starts with '{' opens a prefix block.
            j = i
            while j < len(lines) and (not lines[j].strip(WHITESPACE) or lines[j].lstrip(WHITESPACE)[0] == ';'):
                j += 1
            if j < len(lines) and lines[j].lstrip(WHITESPACE).startswith('{'):
                prefixes.insert(0, name)
                i = j + 1
                continue

            value = _strip_value(rest.lstrip(WHITESPACE))
            if prefixes and '@' in value:
                value = self._at(value, prefixes)

            low = name.lower()
            if low in KEYWORDS:
                raise Unsupported('%s: "%s" blocks are not modelled' % (path, word))
            if low == 'include':
                target = self.expand(value).strip('"\'')
                if not os.path.isabs(target):
                    target = os.path.join(self.root, target)
                if os.path.normcase(os.path.basename(target)) == 'personal.rc' and self._personal:
                    target = self._personal
                self._parse_file(target)
                continue
            if name.startswith('*') or low in MULTI_KEYS:
                self.stars.setdefault(low, []).append(value)
                continue
            for suffix in CHAIN_SUFFIXES:
                if name.endswith(suffix) and len(name) > len(suffix):
                    self.chained.setdefault(low, []).append(value)
            self._define(name, value)
        self._including.pop()

    @staticmethod
    def _at(value, prefixes):
        out = []
        i = 0
        while i < len(value):
            if value[i] == '@':
                level = 0
                while i + 1 < len(value) and value[i + 1] == '@':
                    level += 1
                    i += 1
                if level < len(prefixes):
                    out.append(prefixes[level])
                i += 1
            else:
                out.append(value[i])
                i += 1
        return ''.join(out)

    # -- reading -----------------------------------------------------------------------

    def expand(self, value, seen=None):
        """$Var$ -> the variable's value, expanded in turn (VarExpansionEx)."""
        seen = seen or set()

        def repl(m):
            var = m.group(1)
            low = var.lower()
            if low in seen:
                return m.group(0)
            if low in self.settings:
                # VarExpansionEx substitutes GetTokenW(value), not the raw value.
                return self.expand(get_token(self.settings[low][1]), seen | {low})
            env = os.environ.get(var)
            if env is not None:
                return env
            self.unresolved.add(low)
            return m.group(0)

        return re.sub(r'\$([^$\s"]+)\$', repl, value)

    def resolve(self, name, _chain=None):
        """The value a module gets for this name, falling back through <Prefix>Group."""
        low = name.lower()
        if low in self.settings:
            return self.expand(self.settings[low][1])
        chain = _chain or set()
        for i in range(len(low) - 1, 0, -1):
            prefix = low[:i]
            group_key = prefix + 'group'
            if group_key in self.settings and prefix not in chain:
                group = self.expand(self.settings[group_key][1]).strip('"').lower()
                if not group:
                    continue
                found = self.resolve(group + low[i:], chain | {prefix})
                if found is not None:
                    return found
        return None

    def names(self):
        """Every defined name, plus what each <Prefix>Group makes reachable under Prefix."""
        names = set(self.settings)
        groups = []
        for low in self.settings:
            if low.endswith('group') and len(low) > 5:
                target = self.expand(self.settings[low][1]).strip('"').lower()
                if target:
                    groups.append((low[:-5], target))
        for _ in range(8):
            added = set()
            for prefix, target in groups:
                for n in names:
                    if n.startswith(target) and n != target:
                        candidate = prefix + n[len(target):]
                        if candidate not in names and len(candidate) <= MAX_NAME:
                            added.add(candidate)
            if not added:
                break
            names |= added
        return names

    def chained_lines(self, low, suffix):
        """Settings::IterateOverCommandLines: the lines of <P><suffix>, then those of each
        group in P's chain (a group is read directly from <P>Group, without fallback)."""
        prefix = low[:-len(suffix)]
        lines, seen = [], set()
        while prefix and prefix not in seen:
            seen.add(prefix)
            lines.extend(self.chained.get(prefix + suffix.lower(), []))
            group = self.settings.get(prefix + 'group')
            prefix = self.expand(group[1]).strip('"').lower() if group else ''
        return lines

    def dump(self):
        out = []
        chain_names = set()
        for low in self.names():
            for suffix in CHAIN_SUFFIXES:
                if low.endswith(suffix.lower()) and low in self.chained:
                    chain_names.add((low, suffix))
        # Names that only get handlers through a group still have the group's lines.
        for prefix_key in [k for k in self.settings if k.endswith('group')]:
            prefix = prefix_key[:-5]
            for suffix in CHAIN_SUFFIXES:
                if self.chained_lines(prefix + suffix.lower(), suffix):
                    chain_names.add((prefix + suffix.lower(), suffix))
        for low, suffix in chain_names:
            for n, line in enumerate(self.chained_lines(low, suffix)):
                out.append('L %s %d = %s' % (low, n, self.expand(line)))
        for low in self.names():
            value = self.resolve(low)
            if value is not None:
                out.append('S %s = %s' % (low, value))
        for key, lines in self.stars.items():
            for n, line in enumerate(lines):
                out.append('L %s %d = %s' % (key, n, self.expand(line)))
        out.extend('U %s' % v for v in self.unresolved)
        # Paths under the tree's own folder are written relative to it, so trees in two
        # different folders (a baseline from another checkout) compare equal.
        root = re.compile(re.escape(self.root.rstrip('\\/') + '\\'), re.IGNORECASE)
        return sorted(set(root.sub(lambda m: '<root>\\', line) for line in out))


def load(root, theme, personal=None, system_language='en'):
    return Tree(root, theme, personal, system_language)


def _split(lines):
    entries = {}
    for line in lines:
        key, sep, value = line.partition(' = ')
        entries[key] = value if sep else ''
    return entries


def diff(before, after, ignore_new_names=()):
    """(exit code, report lines). New names under ignore_new_names prefixes, and new
    '...group' keys (their effect shows in the names they make reachable), are not
    differences."""
    a, b = _split(before), _split(after)
    report = []
    for key in sorted(set(a) | set(b)):
        if key in a and key in b:
            if a[key] != b[key]:
                report.append('~ %s\n    before: %s\n    after:  %s' % (key, a[key], b[key]))
        elif key in a:
            report.append('- %s = %s' % (key, a[key]))
        else:
            name = key[2:].split(' ')[0] if key[:2] in ('S ', 'L ') else key[2:]
            if key.startswith('S ') and (name.endswith('group') or
                                         any(name.startswith(p.lower()) for p in ignore_new_names)):
                continue
            report.append('+ %s = %s' % (key, b[key]))
    return (1 if report else 0), report


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest='command', required=True)
    d = sub.add_parser('dump')
    d.add_argument('--root', required=True)
    d.add_argument('--theme', required=True)
    d.add_argument('--personal')
    d.add_argument('--system-language', default='en', help='what lsapi detected (default en)')
    c = sub.add_parser('diff')
    c.add_argument('before')
    c.add_argument('after')
    c.add_argument('--ignore-new', action='append', default=[],
                   help='name prefix of new style keys that are not differences (repeatable)')
    args = parser.parse_args(argv)

    if args.command == 'dump':
        sys.stdout.reconfigure(encoding='utf-8')
        for line in load(args.root, args.theme, args.personal, args.system_language).dump():
            print(line)
        return 0

    def read(path):
        with open(path, encoding='utf-8') as f:
            return [l.rstrip('\n') for l in f if l.strip()]
    code, report = diff(read(args.before), read(args.after), args.ignore_new)
    sys.stdout.reconfigure(encoding='utf-8')
    for line in report:
        print(line)
    print('equal' if code == 0 else '%d difference(s)' % len(report))
    return code


if __name__ == '__main__':
    sys.exit(main())
