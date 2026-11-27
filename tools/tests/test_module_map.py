"""config\\module-map.rc maps the module names of classic themes (label-1.99, popup2, vtray...)
to the repo's nModules: one `*ModuleAlias <name> <Target>` line each. A name is compared in
its normalised form, the rule below; fixtures\\module-names.txt pins that rule for the C++
and PowerShell copies of it."""
import os
import re
import unittest

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..')
MAP = os.path.join(ROOT, 'config', 'module-map.rc')
MODULES = os.path.join(ROOT, 'modules')
FIXTURE = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'fixtures', 'module-names.txt')
KEY = '*ModuleAlias'


def normalise(name):
    """File name only (everything up to the last \\, / or $ is a directory), lower case,
    no .dll, and no trailing '-' + version that starts with a digit."""
    name = re.split(r'[\\/$]', name)[-1].lower()
    if name.endswith('.dll'):
        name = name[:-4]
    return re.sub(r'-\d.*$', '', name)


def content_lines(path):
    with open(path, encoding='utf-8') as f:
        return [l.strip() for l in f if l.strip() and not l.lstrip().startswith(';')]


def aliases():
    result = []
    for line in content_lines(MAP):
        parts = line.split()
        result.append((line, parts))
    return result


class ModuleMap(unittest.TestCase):
    def test_every_line_is_a_moduleAlias_line(self):
        entries = aliases()
        self.assertTrue(entries, 'module map is empty')
        for line, parts in entries:
            with self.subTest(line=line):
                self.assertEqual(len(parts), 3, '"%s" is not "%s <name> <Target>"' % (line, KEY))
                self.assertEqual(parts[0], KEY)

    def test_targets_have_a_module_project(self):
        for line, parts in aliases():
            with self.subTest(line=line):
                target = parts[2]
                self.assertTrue(target == 'Core' or os.path.isdir(os.path.join(MODULES, target)),
                                'no modules\\%s for "%s"' % (target, line))

    def test_no_alias_targets_desk(self):
        for line, parts in aliases():
            with self.subTest(line=line):
                self.assertNotEqual(parts[2].lower(), 'desk')

    def test_no_duplicate_sources(self):
        seen = set()
        for line, parts in aliases():
            with self.subTest(line=line):
                source = parts[1].lower()
                self.assertNotIn(source, seen)
                seen.add(source)

    def test_sources_are_normalised(self):
        for line, parts in aliases():
            with self.subTest(line=line):
                self.assertEqual(parts[1], normalise(parts[1]))

    def test_fixture_is_well_formed(self):
        cases = content_lines(FIXTURE)
        self.assertTrue(cases, 'fixture is empty')
        for line in cases:
            with self.subTest(line=line):
                self.assertEqual(line.count(' -> '), 1, '"%s" is not "<input> -> <normalised>"' % line)
                given, expected = (s.strip() for s in line.split(' -> '))
                self.assertTrue(given and expected)
                self.assertEqual(normalise(given), expected)
                self.assertEqual(normalise(expected), expected)


if __name__ == '__main__':
    unittest.main()
