"""tools\\theme-plan.ps1 reports, per theme, which modules and fonts are ok, mapped to an
nModule, missing or blocked (the plan file a future downloader reads; spec
docs\\superpowers\\specs\\2026-11-26-theme-install-design.md section 3).

Each folder under fixtures\\plan\\ is a theme: theme.rc (+ included files, theme.json),
present.txt (the DLLs that exist in the run folder) and expected.json (the whole plan
except `generated`). run-plan.ps1 builds a scratch run folder for the case and writes
the plan. The PowerShell cases are skipped when neither pwsh nor powershell exists."""
import glob
import json
import os
import shutil
import subprocess
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..', '..'))
CASES = os.path.join(HERE, 'fixtures', 'plan')
RUN_PLAN = os.path.join(HERE, 'run-plan.ps1')
THEME_PLAN = os.path.join(ROOT, 'tools', 'theme-plan.ps1')
NAMES = os.path.join(HERE, 'fixtures', 'module-names.txt')
STATUSES = {'ok', 'mapped', 'missing', 'blocked'}
KINDS = {'module', 'font'}
FIELDS = {'kind', 'name', 'status', 'resolved', 'note', 'reason', 'url', 'sha256'}


def powershell():
    """The PowerShell command line prefix, or None."""
    # Windows PowerShell 5.1 first: it is what the tools must run on.
    for exe in ('powershell', 'pwsh'):
        path = shutil.which(exe)
        if path:
            return [path, '-NoProfile', '-NonInteractive', '-ExecutionPolicy', 'Bypass']
    return None


def cases():
    return sorted(d for d in os.listdir(CASES) if os.path.isdir(os.path.join(CASES, d)))


def load(path):
    with open(path, encoding='utf-8') as f:
        return json.load(f)


class ExpectedFiles(unittest.TestCase):
    def test_expected_files_are_valid(self):
        self.assertTrue(cases(), 'no plan fixtures')
        for case in cases():
            with self.subTest(case=case):
                plan = load(os.path.join(CASES, case, 'expected.json'))
                self.assertEqual(plan['theme'], case)
                self.assertNotIn('generated', plan)
                self.assertIsInstance(plan['items'], list)
                self.assertIsInstance(plan['warnings'], list)
                for item in plan['items']:
                    self.assertIn(item['kind'], KINDS)
                    self.assertIn(item['status'], STATUSES)
                    self.assertTrue(item['name'])
                    self.assertLessEqual(set(item), FIELDS)
                    if item['status'] == 'mapped':
                        self.assertIn('resolved', item)
                    if item['status'] == 'blocked':
                        self.assertIn('reason', item)
                    if item['status'] == 'missing':
                        self.assertIn('url', item)

    def test_every_case_has_a_theme_and_a_present_list(self):
        for case in cases():
            with self.subTest(case=case):
                self.assertTrue(os.path.isfile(os.path.join(CASES, case, 'theme.rc')))
                self.assertTrue(os.path.isfile(os.path.join(CASES, case, 'present.txt')))

    def test_repo_manifests_are_valid(self):
        manifests = glob.glob(os.path.join(ROOT, 'themes', '*', 'theme.json'))
        self.assertTrue(manifests, 'no theme.json under themes\\')
        for path in manifests:
            with self.subTest(theme=os.path.basename(os.path.dirname(path))):
                with open(path, 'rb') as f:
                    self.assertFalse(f.read(3) == b'\xef\xbb\xbf', 'BOM')
                manifest = load(path)
                self.assertEqual(manifest['name'], os.path.basename(os.path.dirname(path)))
                self.assertIn(manifest['mode'], ('overlay', 'shell'))
                for module in manifest['requires']['modules']:
                    self.assertTrue(os.path.isdir(os.path.join(ROOT, 'modules', module['name'])),
                                    'no modules\\%s' % module['name'])
                for font in manifest['requires']['fonts']:
                    self.assertTrue(font['name'])


class Plans(unittest.TestCase):
    def setUp(self):
        self.ps = powershell()
        if self.ps is None:
            self.skipTest('neither pwsh nor powershell is installed')

    def test_plan_cases(self):
        for case in cases():
            with self.subTest(case=case), tempfile.TemporaryDirectory() as tmp:
                out = os.path.join(tmp, 'plan.json')
                run = subprocess.run(self.ps + ['-File', RUN_PLAN, os.path.join(CASES, case), out],
                                     capture_output=True, text=True, errors='replace')
                self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
                with open(out, 'rb') as f:
                    raw = f.read()
                self.assertFalse(raw.startswith(b'\xef\xbb\xbf'), 'plan written with a BOM')
                actual = json.loads(raw.decode('utf-8'))
                self.assertRegex(actual.pop('generated'), r'^\d{4}-\d\d-\d\dT\d\d:\d\d:\d\dZ$')
                expected = load(os.path.join(CASES, case, 'expected.json'))
                self.assertEqual(actual, expected)

    def test_module_key_matches_fixture(self):
        pairs = []
        with open(NAMES, encoding='utf-8') as f:
            for line in f:
                line = line.strip()
                if line and not line.startswith(';'):
                    given, expected = (s.strip() for s in line.split(' -> '))
                    pairs.append((given, expected))
        self.assertTrue(pairs)
        script = ('. "%s"; Get-Content -LiteralPath "%s" -Encoding UTF8 | '
                  'ForEach-Object { ConvertTo-ModuleKey $_ }') % (THEME_PLAN, '%s')
        with tempfile.TemporaryDirectory() as tmp:
            inputs = os.path.join(tmp, 'inputs.txt')
            with open(inputs, 'w', encoding='utf-8') as f:
                f.write('\n'.join(given for given, _ in pairs) + '\n')
            run = subprocess.run(self.ps + ['-Command', script % inputs],
                                 capture_output=True, text=True, errors='replace')
        self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
        keys = run.stdout.splitlines()
        self.assertEqual(len(keys), len(pairs), run.stdout + run.stderr)
        for (given, expected), key in zip(pairs, keys):
            with self.subTest(name=given):
                self.assertEqual(key, expected)


if __name__ == '__main__':
    unittest.main()
