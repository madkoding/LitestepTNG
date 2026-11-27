"""tools\\install-theme.ps1 copies a theme (folder or zip) into a run folder and writes its
plan; tools\\switch-theme.ps1 and tools\\deploy.ps1 write the plan too (spec
docs\\superpowers\\specs\\2026-11-26-theme-install-design.md section 4).

Every case uses a scratch run folder (an empty litestep.exe, a copy of config\\module-map.rc
and an empty Label.dll); none touches the real one. The zips are built here with zipfile,
including one whose entry escapes the target ("../evil.dll"). The cases are skipped when
neither powershell nor pwsh exists."""
import json
import os
import shutil
import subprocess
import tempfile
import unittest
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..', '..'))
TOOLS = os.path.join(ROOT, 'tools')
FOLDER_THEME = os.path.join(HERE, 'fixtures', 'install', 'folder-theme')


def powershell():
    """The PowerShell command line prefix, or None. Windows PowerShell 5.1 first: it is what
    the tools must run on."""
    for exe in ('powershell', 'pwsh'):
        path = shutil.which(exe)
        if path:
            return [path, '-NoProfile', '-NonInteractive', '-ExecutionPolicy', 'Bypass']
    return None


def listing(folder):
    """Every file and folder under folder, relative, sorted."""
    found = []
    for base, dirs, files in os.walk(folder):
        for name in dirs + files:
            found.append(os.path.relpath(os.path.join(base, name), folder))
    return sorted(found)


class ScratchRunFolder(unittest.TestCase):
    def setUp(self):
        self.ps = powershell()
        if self.ps is None:
            self.skipTest('neither powershell nor pwsh is installed')
        self.tmp = tempfile.mkdtemp(prefix='ls-install-test-')
        self.addCleanup(shutil.rmtree, self.tmp, True)
        self.run_dir = os.path.join(self.tmp, 'run')
        os.makedirs(os.path.join(self.run_dir, 'config'))
        os.makedirs(os.path.join(self.run_dir, 'themes'))
        for name in ('litestep.exe', 'Label.dll'):
            open(os.path.join(self.run_dir, name), 'wb').close()
        shutil.copy(os.path.join(ROOT, 'config', 'module-map.rc'),
                    os.path.join(self.run_dir, 'config', 'module-map.rc'))
        self.src = os.path.join(self.tmp, 'src')
        os.makedirs(self.src)

    def tool(self, script, *args):
        return subprocess.run(self.ps + ['-File', os.path.join(TOOLS, script)] + list(args)
                              + ['-LiteStepDir', self.run_dir],
                              capture_output=True, text=True, errors='replace')

    def install(self, source, *args):
        return self.tool('install-theme.ps1', '-Source', source, *args)

    def theme_path(self, name, *rest):
        return os.path.join(self.run_dir, 'themes', name, *rest)

    def plan(self, name):
        path = os.path.join(self.run_dir, 'plans', name + '.json')
        self.assertTrue(os.path.isfile(path), 'no plan ' + path)
        with open(path, 'rb') as f:
            raw = f.read()
        self.assertFalse(raw.startswith(b'\xef\xbb\xbf'), 'plan written with a BOM')
        return json.loads(raw.decode('utf-8'))

    def assert_ok(self, run):
        self.assertEqual(run.returncode, 0, run.stdout + run.stderr)

    def assert_refused(self, run):
        self.assertNotEqual(run.returncode, 0, run.stdout + run.stderr)

    def copy_theme(self, name):
        """A copy of the folder fixture named name, under the source folder."""
        target = os.path.join(self.src, name)
        shutil.copytree(FOLDER_THEME, target)
        return target

    def make_zip(self, name, entries):
        """A zip in the source folder; entries maps entry name -> text."""
        path = os.path.join(self.src, name)
        with zipfile.ZipFile(path, 'w') as z:
            for entry, text in entries.items():
                z.writestr(entry, text)
        return path


class InstallTheme(ScratchRunFolder):
    THEME_RC = open(os.path.join(FOLDER_THEME, 'theme.rc'), encoding='utf-8').read()

    def test_folder_theme_is_copied_and_planned(self):
        run = self.install(FOLDER_THEME)
        self.assert_ok(run)
        self.assertTrue(os.path.isfile(self.theme_path('folder-theme', 'theme.rc')))
        plan = self.plan('folder-theme')
        self.assertEqual(plan['theme'], 'folder-theme')
        status = {i['name']: i['status'] for i in plan['items']}
        self.assertEqual(status, {'Label': 'ok', 'geekamp-1.9.7': 'missing'})
        self.assertIn("Theme 'folder-theme'", run.stdout)
        # Installing neither selects the theme nor writes anything else.
        self.assertFalse(os.path.exists(os.path.join(self.run_dir, 'theme-select.rc')))

    def test_wrapped_zip_is_unwrapped(self):
        z = self.make_zip('download.zip', {'Wrapped Theme/theme.rc': self.THEME_RC,
                                           'Wrapped Theme/images/a.txt': 'x'})
        self.assert_ok(self.install(z))
        self.assertTrue(os.path.isfile(self.theme_path('Wrapped Theme', 'theme.rc')))
        self.assertTrue(os.path.isfile(self.theme_path('Wrapped Theme', 'images', 'a.txt')))
        self.assertEqual(self.plan('Wrapped Theme')['theme'], 'Wrapped Theme')

    def test_root_zip_takes_the_manifest_name(self):
        z = self.make_zip('whatever.zip', {'theme.rc': self.THEME_RC,
                                           'theme.json': json.dumps({'name': 'Named Theme'})})
        self.assert_ok(self.install(z))
        self.assertTrue(os.path.isfile(self.theme_path('Named Theme', 'theme.rc')))
        self.plan('Named Theme')

    def test_root_zip_without_manifest_takes_the_zip_name(self):
        z = self.make_zip('ZipNamed.zip', {'theme.rc': self.THEME_RC})
        self.assert_ok(self.install(z))
        self.assertTrue(os.path.isfile(self.theme_path('ZipNamed', 'theme.rc')))

    def test_zip_traversal_is_refused_and_writes_nothing(self):
        z = self.make_zip('zip-traversal.zip', {'Evil/theme.rc': self.THEME_RC,
                                                '../evil.dll': 'MZ',
                                                'Evil/../../evil2.dll': 'MZ'})
        before = listing(self.run_dir)
        run = self.install(z)
        self.assert_refused(run)
        self.assertEqual(listing(self.run_dir), before)
        for parent in (self.tmp, self.src, os.path.dirname(self.run_dir)):
            self.assertFalse(os.path.exists(os.path.join(parent, 'evil.dll')))
            self.assertFalse(os.path.exists(os.path.join(parent, 'evil2.dll')))

    def test_backslash_traversal_is_refused(self):
        path = os.path.join(self.src, 'backslash.zip')
        with zipfile.ZipFile(path, 'w') as z:
            z.writestr('theme.rc', self.THEME_RC)
            info = zipfile.ZipInfo('x')
            info.filename = '..\\evil.dll'  # bypasses ZipInfo's separator conversion
            z.writestr(info, 'MZ')
        before = listing(self.run_dir)
        self.assert_refused(self.install(path))
        self.assertEqual(listing(self.run_dir), before)

    def test_source_without_theme_rc_is_refused(self):
        os.makedirs(os.path.join(self.src, 'NotATheme'))
        before = listing(self.run_dir)
        self.assert_refused(self.install(os.path.join(self.src, 'NotATheme')))
        z = self.make_zip('two.zip', {'a/theme.rc': self.THEME_RC, 'b/theme.rc': self.THEME_RC})
        self.assert_refused(self.install(z))
        self.assertEqual(listing(self.run_dir), before)

    def test_bad_manifest_name_is_refused(self):
        for name in ('..\\up', '../up', 'a/b', 'a:b', '..', ''):
            with self.subTest(name=name):
                z = self.make_zip('bad.zip', {'theme.rc': self.THEME_RC,
                                              'theme.json': json.dumps({'name': name})})
                if name == '':
                    # An empty name is no name: the zip's file name is used.
                    self.assert_ok(self.install(z, '-Force'))
                    self.assertTrue(os.path.isfile(self.theme_path('bad', 'theme.rc')))
                    continue
                before = listing(self.run_dir)
                self.assert_refused(self.install(z))
                self.assertEqual(listing(self.run_dir), before)

    def test_leading_dot_name_is_refused(self):
        z = self.make_zip('.hidden.zip', {'theme.rc': self.THEME_RC})
        before = listing(self.run_dir)
        self.assert_refused(self.install(z))
        self.assertEqual(listing(self.run_dir), before)

    def test_name_with_brackets_is_installed_and_planned(self):
        self.assert_ok(self.install(self.copy_theme('Neo [v2]')))
        self.assertTrue(os.path.isfile(self.theme_path('Neo [v2]', 'theme.rc')))
        self.assertEqual(self.plan('Neo [v2]')['theme'], 'Neo [v2]')

    def test_unc_source(self):
        unc = '\\\\localhost\\' + FOLDER_THEME[0] + '$' + FOLDER_THEME[2:]
        if not os.path.isdir(unc):
            self.skipTest('the administrative share is not reachable')
        self.assert_ok(self.install(unc))
        self.assertTrue(os.path.isfile(self.theme_path('folder-theme', 'theme.rc')))

    def test_second_install_needs_force(self):
        self.assert_ok(self.install(FOLDER_THEME))
        marker = self.theme_path('folder-theme', 'old.txt')
        open(marker, 'w').close()
        self.assert_refused(self.install(FOLDER_THEME))
        self.assertTrue(os.path.exists(marker))
        self.assert_ok(self.install(FOLDER_THEME, '-Force'))
        self.assertTrue(os.path.isfile(self.theme_path('folder-theme', 'theme.rc')))
        self.assertFalse(os.path.exists(marker), '-Force must replace the folder, not merge')

    def test_name_clash_ignores_case(self):
        self.assert_ok(self.install(self.copy_theme('Glass')))
        other = os.path.join(self.tmp, 'other')
        os.makedirs(other)
        lower = os.path.join(other, 'glass')
        shutil.copytree(FOLDER_THEME, lower)
        self.assert_refused(self.install(lower))
        # With -Force the folder keeps the spelling it was installed with.
        self.assert_ok(self.install(lower, '-Force'))
        self.assertEqual([d for d in os.listdir(os.path.join(self.run_dir, 'themes'))], ['Glass'])
        self.plan('Glass')

    def test_manifest_name_mismatch_is_a_warning(self):
        folder = self.copy_theme('FolderName')
        with open(os.path.join(folder, 'theme.json'), 'w', encoding='utf-8') as f:
            json.dump({'name': 'Other Name'}, f)
        run = self.install(folder)
        self.assert_ok(run)
        self.assertTrue(os.path.isfile(self.theme_path('FolderName', 'theme.rc')))
        self.assertIn('Other Name', run.stdout + run.stderr)


class SwitchAndDeploy(ScratchRunFolder):
    def test_switch_writes_the_plan_then_selects(self):
        self.assert_ok(self.install(FOLDER_THEME))
        os.remove(os.path.join(self.run_dir, 'plans', 'folder-theme.json'))
        run = self.tool('switch-theme.ps1', 'folder-theme', '-NoRestart')
        self.assert_ok(run)
        self.assertEqual(self.plan('folder-theme')['theme'], 'folder-theme')
        self.assertIn("Theme 'folder-theme'", run.stdout)
        self.assertIn('missing', (run.stdout + run.stderr).lower())
        with open(os.path.join(self.run_dir, 'theme-select.rc'), encoding='utf-8') as f:
            self.assertEqual(f.read().strip(), 'ThemeName "folder-theme"')

    def test_switch_list_mode_still_lists(self):
        self.assert_ok(self.install(FOLDER_THEME))
        run = self.tool('switch-theme.ps1')
        self.assert_ok(run)
        self.assertIn('folder-theme', run.stdout)
        self.assertFalse(os.path.exists(os.path.join(self.run_dir, 'theme-select.rc')))

    def leftover(self):
        """A helper folder install-theme.ps1 can leave behind (with a theme.rc in it)."""
        shutil.copytree(FOLDER_THEME, self.theme_path('.replaced-0123'))

    def test_switch_skips_helper_folders_and_handles_brackets(self):
        self.leftover()
        self.assert_ok(self.install(self.copy_theme('Neo [v2]')))
        run = self.tool('switch-theme.ps1')
        self.assert_ok(run)
        self.assertIn('Neo [v2]', run.stdout)
        self.assertNotIn('.replaced-0123', run.stdout)
        os.remove(os.path.join(self.run_dir, 'plans', 'Neo [v2].json'))
        self.assert_ok(self.tool('switch-theme.ps1', 'Neo [v2]', '-NoRestart'))
        self.plan('Neo [v2]')
        self.assert_refused(self.tool('switch-theme.ps1', '.replaced-0123', '-NoRestart'))

    def test_deploy_skips_helper_folders_and_plans_brackets(self):
        self.leftover()
        self.assert_ok(self.install(self.copy_theme('Neo [v2]')))
        os.remove(os.path.join(self.run_dir, 'plans', 'Neo [v2].json'))
        self.assert_ok(self.tool('deploy.ps1', '-ThemesOnly', '-NoRestart'))
        self.plan('Neo [v2]')
        self.assertFalse(os.path.exists(os.path.join(self.run_dir, 'plans', '.replaced-0123.json')))

    def test_deploy_themes_only_writes_plans(self):
        os.makedirs(self.theme_path('Glass'))
        run = self.tool('deploy.ps1', '-ThemesOnly', '-NoRestart')
        self.assert_ok(run)
        self.assertTrue(os.path.isfile(self.theme_path('Glass', 'theme.rc')))
        self.assertEqual(self.plan('Glass')['theme'], 'Glass')


if __name__ == '__main__':
    unittest.main()
