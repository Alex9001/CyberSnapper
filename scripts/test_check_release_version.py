"""Exercise the version gate using isolated copies of the release metadata."""
from pathlib import Path
import json
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
VERSION = json.loads((ROOT / 'package.json').read_text())['version']


@unittest.skipUnless(shutil.which('node'), 'Node.js is required for the release version gate')
class VersionGateTest(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        self.root = Path(temp.name)
        for name in ('scripts/check-release-version.mjs', 'package.json', 'package-lock.json',
                     'CMakeLists.txt', 'site/index.html',
                     'native/packaging/net.cyberbrand.CyberSnapper.desktop',
                     'native/packaging/net.cyberbrand.CyberSnapper.metainfo.xml'):
            destination = self.root / name
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(ROOT / name, destination)

    def gate(self, tag='v' + VERSION):
        return subprocess.run(['node', str(self.root / 'scripts/check-release-version.mjs'), tag],
                              text=True, capture_output=True, timeout=10)

    def test_current_version_matches_every_source(self):
        result = self.gate()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('Release version verified', result.stdout)

    def test_old_tag_is_rejected(self):
        self.assertNotEqual(self.gate('v0.0.0').returncode, 0)

    def test_rehearsal_needs_no_tag(self):
        result = self.gate('')
        self.assertEqual(result.returncode, 0)
        self.assertIn('rehearsal', result.stdout)

    def test_metadata_mismatch_fails_without_success_message(self):
        for name in ('package-lock.json', 'CMakeLists.txt', 'site/index.html',
                     'native/packaging/net.cyberbrand.CyberSnapper.desktop',
                     'native/packaging/net.cyberbrand.CyberSnapper.metainfo.xml'):
            with self.subTest(source=name):
                path = self.root / name
                original = path.read_text()
                path.write_text(original.replace(VERSION, '0.0.0'))
                result = self.gate()
                self.assertNotEqual(result.returncode, 0)
                self.assertNotIn('Release version verified', result.stdout)
                path.write_text(original)


if __name__ == '__main__':
    unittest.main()
