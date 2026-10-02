"""Offline tests for release assembly: no missing, extra, or unmatched artifacts."""
import hashlib
import importlib.util
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('release_assets', Path(__file__).with_name('check-release-assets.py'))
checker = importlib.util.module_from_spec(spec)
spec.loader.exec_module(checker)


class ReleaseAssetsTest(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        for name in checker.expected_assets():
            (self.root / name).write_bytes(b'release fixture')
        for arch in ('x64', 'arm64'):
            image = self.root / f'CyberSnapper-linux-{arch}.AppImage'
            fields = {'zsync': '0.6.2', 'Filename': image.name, 'URL': image.name,
                      'Length': str(image.stat().st_size), 'SHA-1': hashlib.sha1(image.read_bytes()).hexdigest(),
                      'Blocksize': '2048', 'Hash-Lengths': '1,2,4'}
            image.with_name(image.name + '.zsync').write_bytes(
                ('\n'.join(f'{key}: {value}' for key, value in fields.items()) + '\n\n').encode() + b'123456')

    def test_complete_assets_and_deterministic_sums(self):
        checker.verify(self.root)
        sums = (self.root / 'SHA256SUMS.txt').read_text()
        self.assertEqual(len(sums.splitlines()), 14)
        self.assertIn('CyberSnapper-linux-arm64.AppImage.zsync', sums)
        checker.verify(self.root)
        self.assertEqual((self.root / 'SHA256SUMS.txt').read_text(), sums)

    def test_missing_sidecar_rejected(self):
        (self.root / 'CyberSnapper-linux-x64.AppImage.zsync').unlink()
        with self.assertRaisesRegex(ValueError, 'missing='):
            checker.verify(self.root)

    def test_unexpected_evidence_rejected(self):
        (self.root / 'startup.log').write_text('evidence belongs in a separate artifact')
        with self.assertRaisesRegex(ValueError, 'unexpected='):
            checker.verify(self.root)

    def test_empty_asset_rejected(self):
        (self.root / 'CyberSnapper-macos-x64.dmg').write_bytes(b'')
        with self.assertRaisesRegex(ValueError, 'empty'):
            checker.verify(self.root)

    def test_mismatched_image_rejected(self):
        (self.root / 'CyberSnapper-linux-x64.AppImage').write_bytes(b'changed fixture')
        with self.assertRaisesRegex(ValueError, 'does not match image'):
            checker.verify(self.root)


if __name__ == '__main__':
    unittest.main()
