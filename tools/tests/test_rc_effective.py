"""Tests for tools/rc_effective.py (run: python -m unittest discover -s tools/tests -v)."""
import os
import sys
import tempfile
import textwrap
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
import rc_effective  # noqa: E402

STEP = 'include "$LiteStepDir$themes\\$ThemeName$\\theme.rc"\n'


class TreeCase(unittest.TestCase):
    """Writes a tiny run folder: step.rc + themes\\T\\<files>."""

    def make(self, files, theme='T'):
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        root = tmp.name
        with open(os.path.join(root, 'step.rc'), 'w', encoding='utf-8') as f:
            f.write(STEP)
        for name, text in files.items():
            path = os.path.join(root, 'themes', theme, *name.split('/'))
            os.makedirs(os.path.dirname(path), exist_ok=True)
            with open(path, 'w', encoding='utf-8') as f:
                f.write(textwrap.dedent(text))
        return root

    def load(self, files, **kw):
        return rc_effective.load(self.make(files), 'T', **kw)

    def diff_exit(self, before, after):
        a = rc_effective.load(self.make(before), 'T').dump()
        b = rc_effective.load(self.make(after), 'T').dump()
        return rc_effective.diff(a, b)[0]


class ResolveTests(TreeCase):
    def test_first_definition_wins_across_includes(self):
        t = self.load({
            'theme.rc': 'include "$LiteStepDir$themes\\T\\a.rc"\ninclude "$LiteStepDir$themes\\T\\b.rc"\n',
            'a.rc': 'XColor red\n',
            'b.rc': 'XColor blue\n',
        })
        self.assertEqual(t.resolve('XColor'), 'red')

    def test_keys_case_insensitive(self):
        t = self.load({'theme.rc': 'xcolor red\n'})
        self.assertEqual(t.resolve('XCOLOR'), 'red')

    def test_trailing_comment_stripped_outside_quotes(self):
        t = self.load({'theme.rc': 'XText "a ; b" ; comment\n'})
        self.assertEqual(t.resolve('XText'), '"a ; b"')

    def test_group_fallback(self):
        t = self.load({'theme.rc': 'LabelGroup Style\nStyleFontSize 8\n'})
        self.assertEqual(t.resolve('LabelFontSize'), '8')
        t = self.load({'theme.rc': 'LabelFontSize 9\nLabelGroup Style\nStyleFontSize 8\n'})
        self.assertEqual(t.resolve('LabelFontSize'), '9')

    def test_group_child_suffix(self):
        t = self.load({'theme.rc': 'MenuGroup Style\nStyleItemFontColor #FF000000\n'})
        self.assertEqual(t.resolve('MenuItemFontColor'), '#FF000000')

    def test_group_chain_and_cycle(self):
        t = self.load({'theme.rc': 'AGroup B\nBGroup C\nCSize 3\n'})
        self.assertEqual(t.resolve('ASize'), '3')
        t = self.load({'theme.rc': 'AGroup B\nBGroup C\nCGroup A\nCSize 3\n'})
        self.assertEqual(t.resolve('ASize'), '3')
        self.assertIsNone(t.resolve('AMissing'))

    def test_variable_expansion_at_read(self):
        t = self.load({'theme.rc': 'XImage "$Img$a.png"\nImg C:\\i\\\n'})
        self.assertEqual(t.resolve('XImage'), '"C:\\i\\a.png"')

    def test_prefix_block(self):
        t = self.load({'theme.rc': textwrap.dedent('''\
            MClock
            {
              X 10
              Text "@ here"
              - shown
              Date
              {
                Y 2
                Parent @@
              }
            }
            ''')})
        self.assertEqual(t.resolve('MClockX'), '10')
        self.assertEqual(t.resolve('MClockText'), '"MClock here"')
        self.assertEqual(t.resolve('MClock'), 'shown')
        self.assertEqual(t.resolve('MClockDateY'), '2')
        self.assertEqual(t.resolve('MClockDateParent'), 'MClock')

    def test_star_line_in_prefix_block(self):
        t = self.load({'theme.rc': 'Bar\n{\n  *OverlayLabel A B\n}\n'})
        self.assertIn('L *baroverlaylabel 0 = A B', t.dump())

    def test_if_unsupported(self):
        with self.assertRaises(rc_effective.Unsupported):
            self.load({'theme.rc': 'if 1\nX 1\nendif\n'})


class DumpDiffTests(TreeCase):
    def test_unresolved_variable_reported(self):
        t = self.load({'theme.rc': 'XText "$Nope$"\n'})
        self.assertIn('U nope', t.dump())

    def test_star_line_order_matters(self):
        a = {'theme.rc': '*Popup "A" !a\n*Popup "B" !b\n'}
        b = {'theme.rc': '*Popup "B" !b\n*Popup "A" !a\n'}
        self.assertEqual(self.diff_exit(a, b), 1)
        self.assertEqual(self.diff_exit(a, a), 0)

    def test_new_inherited_key_is_reported(self):
        before = {'theme.rc': 'LabelX 1\n'}
        after = {'theme.rc': 'LabelX 1\nLabelGroup S\nSTextAlign Center\n'}
        dump_after = rc_effective.load(self.make(after), 'T').dump()
        self.assertIn('S labeltextalign = Center', dump_after)
        self.assertEqual(self.diff_exit(before, after), 1)

    def test_moving_keys_into_a_style_is_equivalent(self):
        before = {'theme.rc': 'AFont hooge\nAFontSize 8\nBFont hooge\nBFontSize 8\n'}
        after = {'theme.rc': 'AGroup S\nBGroup S\nSFont hooge\nSFontSize 8\n'}
        a = rc_effective.load(self.make(before), 'T').dump()
        b = rc_effective.load(self.make(after), 'T').dump()
        exit_code, lines = rc_effective.diff(a, b, ignore_new_names=('s', 'agroup', 'bgroup'))
        self.assertEqual(exit_code, 0, lines)


if __name__ == '__main__':
    unittest.main()
