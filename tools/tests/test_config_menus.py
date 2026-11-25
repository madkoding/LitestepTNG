"""config\\menus\\<name>.rc (*Popup) and <name>-startmenu.rc (*PopupStartMenu) must list the
same entries: they are two copies only because star keys cannot be built from variables.
A piece no start menu uses has no -startmenu variant."""
import glob
import os
import unittest

CONFIG = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'config', 'menus')


def entries(path, key):
    with open(path, encoding='utf-8') as f:
        lines = [l.strip() for l in f if l.strip() and not l.lstrip().startswith(';')]
    for line in lines:
        assert line.startswith(key + ' '), '%s: "%s" is not a %s line' % (path, line, key)
    return [line[len(key) + 1:].strip() for line in lines]


class ConfigMenuVariants(unittest.TestCase):
    def test_variants_have_the_same_entries(self):
        popups = [p for p in glob.glob(os.path.join(CONFIG, '*.rc')) if not p.endswith('-startmenu.rc')]
        self.assertTrue(popups, 'no config menus found')
        for popup in popups:
            startmenu = popup[:-3] + '-startmenu.rc'
            with self.subTest(menu=os.path.basename(popup)):
                entries(popup, '*Popup')
                if os.path.isfile(startmenu):
                    self.assertEqual(entries(popup, '*Popup'), entries(startmenu, '*PopupStartMenu'))

    def test_every_startmenu_variant_has_its_popup(self):
        for startmenu in glob.glob(os.path.join(CONFIG, '*-startmenu.rc')):
            with self.subTest(menu=os.path.basename(startmenu)):
                self.assertTrue(os.path.isfile(startmenu.replace('-startmenu.rc', '.rc')))


if __name__ == '__main__':
    unittest.main()
