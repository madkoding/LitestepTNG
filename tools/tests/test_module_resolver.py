"""Builds tools\\tests\\module_resolver_test.cpp with litestep\\ModuleResolver.cpp (std-only)
and runs it against fixtures\\module-names.txt. Uses g++, else `zig c++`; skipped when
neither is installed."""
import os
import shutil
import subprocess
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..', '..'))
SOURCES = [os.path.join(ROOT, 'litestep', 'ModuleResolver.cpp'),
           os.path.join(HERE, 'module_resolver_test.cpp')]
FIXTURE = os.path.join(HERE, 'fixtures', 'module-names.txt')


def compiler():
    """The command that compiles C++14, or None."""
    if shutil.which('g++'):
        return ['g++']
    if shutil.which('zig'):
        return ['zig', 'c++']
    return None


class ModuleResolver(unittest.TestCase):
    def test_resolver(self):
        cxx = compiler()
        if cxx is None:
            self.skipTest('neither g++ nor zig is installed')
        with tempfile.TemporaryDirectory() as tmp:
            exe = os.path.join(tmp, 'module_resolver_test.exe' if os.name == 'nt'
                               else 'module_resolver_test')
            build = subprocess.run(
                cxx + ['-std=c++14', '-Wall', '-I' + os.path.join(ROOT, 'litestep')]
                + SOURCES + ['-o', exe],
                capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, 'compile failed (%s):\n%s%s'
                             % (' '.join(cxx), build.stdout, build.stderr))
            run = subprocess.run([exe, FIXTURE], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, 'resolver test failed (%s):\n%s%s'
                             % (' '.join(cxx), run.stdout, run.stderr))


if __name__ == '__main__':
    unittest.main()
