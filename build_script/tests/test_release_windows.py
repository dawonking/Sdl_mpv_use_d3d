"""Exercise packaging and failure preservation with a small CMake fixture."""
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


class ReleaseScriptTests(unittest.TestCase):
    def setUp(self):
        project = Path(__file__).resolve().parents[2]
        self.temporary = tempfile.TemporaryDirectory(prefix='release-script-', dir=project / 'build')
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name) / 'project with spaces'
        (self.root / 'build_script').mkdir(parents=True)
        shutil.copy2(project / 'build_script/release_windows.ps1', self.root / 'build_script/release_windows.ps1')
        (self.root / 'resources/nested').mkdir(parents=True)
        (self.root / 'resources/nested/layout.json').write_text('{"fixture": true}')
        for name in ['sdl_window_test.exe', 'SDL3.dll', 'libmpv-2.dll', 'extra.dll']:
            (self.root / name).write_text('package fixture: ' + name)
        (self.root / 'CMakeLists.txt').write_text('''
cmake_minimum_required(VERSION 3.25)
project(ReleaseScriptFixture NONE)
if(EXISTS "${CMAKE_SOURCE_DIR}/fail-build")
    add_custom_target(sdl_window_test COMMAND "${CMAKE_COMMAND}" -E false)
else()
    add_custom_target(sdl_window_test COMMAND "${CMAKE_COMMAND}" -E true)
endif()
install(FILES sdl_window_test.exe SDL3.dll libmpv-2.dll extra.dll DESTINATION . COMPONENT Runtime)
install(DIRECTORY resources DESTINATION . COMPONENT Runtime)
''')
        ninja = shutil.which('ninja') or 'C:/msys64/mingw64/bin/ninja.exe'
        (self.root / 'CMakePresets.json').write_text(json.dumps({
            'version': 6,
            'configurePresets': [{'name': 'windows-msvc-release', 'generator': 'Ninja',
                'binaryDir': '${sourceDir}/build/windows/release',
                'cacheVariables': {'CMAKE_MAKE_PROGRAM': ninja}}],
            'buildPresets': [{'name': 'windows-msvc-release', 'configurePreset': 'windows-msvc-release'}]
        }))
        self.output = self.root / 'Output/windows'

    def run_script(self, success=True):
        result = subprocess.run(['powershell', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File',
            str(self.root / 'build_script/release_windows.ps1')], cwd=self.temporary.name,
            capture_output=True, text=True, errors='replace', timeout=60)
        self.assertEqual(result.returncode == 0, success, result.stdout + result.stderr)

    def test_external_working_directory_and_complete_package(self):
        self.run_script()
        self.assertEqual(sorted(p.name for p in self.output.glob('*.dll')),
                         ['SDL3.dll', 'extra.dll', 'libmpv-2.dll'])
        self.assertEqual((self.output / 'resources/nested/layout.json').read_text(), '{"fixture": true}')
        self.assertTrue((self.output / 'sdl_window_test.exe').is_file())

    def test_success_removes_stale_generated_files(self):
        self.run_script()
        (self.output / 'stale.dll').write_text('old')
        (self.root / 'resources/nested/layout.json').unlink()
        self.run_script()
        self.assertFalse((self.output / 'stale.dll').exists())
        self.assertFalse((self.output / 'resources/nested/layout.json').exists())

    def test_failed_build_preserves_previous_output(self):
        self.output.mkdir(parents=True)
        (self.output / 'previous.txt').write_text('keep me')
        (self.root / 'fail-build').touch()
        self.run_script(success=False)
        self.assertEqual((self.output / 'previous.txt').read_text(), 'keep me')


if __name__ == '__main__':
    unittest.main()
