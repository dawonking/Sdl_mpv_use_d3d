"""Focused failure-path checks; no toolchain, downloads or SDK writes."""
import importlib.util
import hashlib
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('bootstrap', Path(__file__).parents[1] / 'build_windows.py')
bootstrap = importlib.util.module_from_spec(spec)
spec.loader.exec_module(bootstrap)


class BootstrapTests(unittest.TestCase):
    def test_missing_dependency_stops(self):
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaisesRegex(RuntimeError, 'Missing required file'):
                bootstrap.require_file(Path(directory) / 'missing.dll')

    def test_version_mismatch_stops(self):
        with self.assertRaisesRegex(RuntimeError, 'expected'):
            bootstrap.require_equal('libavcodec', '63.0', '62.11.100')

    def test_native_failure_stops(self):
        import sys
        with self.assertRaisesRegex(RuntimeError, 'Command failed'):
            bootstrap.run([sys.executable, '-c', 'raise SystemExit(7)'])

    def test_hung_test_is_terminated(self):
        import subprocess
        import sys
        with self.assertRaises(subprocess.TimeoutExpired):
            bootstrap.run([sys.executable, '-c', 'import time; time.sleep(5)'], timeout=0.1)

    def test_existing_patch_accepts_crlf_but_not_edits(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory)
            file = source / 'code.c'
            file.write_bytes(b'int value;\r\n')
            spec = {'files': {'code.c': hashlib.sha256(b'int value;\n').hexdigest()}, 'added_files': []}
            with patch.object(bootstrap, 'run', side_effect=['code.c', '']):
                bootstrap.validate_patched_source(source, spec, 'git')
            file.write_text('int changed;\n')
            with patch.object(bootstrap, 'run', side_effect=['code.c', '']):
                with self.assertRaisesRegex(RuntimeError, 'Patched source'):
                    bootstrap.validate_patched_source(source, spec, 'git')

    def test_unexpected_source_change_stops(self):
        with patch.object(bootstrap, 'run', side_effect=['unrelated.c', '']):
            with self.assertRaisesRegex(RuntimeError, 'Unexpected changes'):
                bootstrap.validate_patched_source(Path('.'), {'files': {}, 'added_files': []}, 'git')


if __name__ == '__main__':
    unittest.main()
