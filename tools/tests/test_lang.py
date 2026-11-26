"""Language files: every lang\\<code>.rc (and every themes\\<Name>\\lang\\<code>.rc) has exactly the
keys of the en.rc next to it, every value is quoted, and every Txt variable a theme or config
file uses exists in lang\\en.rc."""
import glob
import os
import re
import unittest

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
CODES = ['en', 'es', 'pt', 'fr', 'de', 'it', 'zh', 'ja']


def settings(path):
    out = {}
    with open(path, encoding='utf-8') as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith(';'):
                continue
            key, _, value = line.partition(' ')
            out[key.lower()] = value.strip()
    return out


def lang_dirs():
    dirs = [os.path.join(ROOT, 'lang')]
    dirs += [d for d in glob.glob(os.path.join(ROOT, 'themes', '*', 'lang')) if os.path.isdir(d)]
    return dirs


class LanguageFiles(unittest.TestCase):
    def test_every_language_has_every_key(self):
        for d in lang_dirs():
            en = settings(os.path.join(d, 'en.rc'))
            for code in CODES:
                path = os.path.join(d, code + '.rc')
                with self.subTest(file=path):
                    self.assertTrue(os.path.isfile(path), 'missing ' + path)
                    self.assertEqual(sorted(settings(path)), sorted(en))

    def test_text_values_quoted(self):
        for path in glob.glob(os.path.join(ROOT, 'lang', '*.rc')):
            for key, value in settings(path).items():
                with self.subTest(file=path, key=key):
                    self.assertRegex(value, r'^"[^"]*"$')

    def test_used_texts_exist(self):
        known = set(settings(os.path.join(ROOT, 'lang', 'en.rc')))
        files = glob.glob(os.path.join(ROOT, 'config', '**', '*.rc'), recursive=True)
        files += [p for p in glob.glob(os.path.join(ROOT, 'themes', '*', '*.rc'))]
        for path in files:
            with open(path, encoding='utf-8') as f:
                used = set(m.lower() for m in re.findall(r'\$(Txt[A-Za-z]+)\$', f.read()))
            with self.subTest(file=path):
                self.assertEqual(sorted(used - known), [])


if __name__ == '__main__':
    unittest.main()
