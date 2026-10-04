#!/usr/bin/env python3
"""Validate the complete release asset set and write deterministic SHA-256 sums."""
import argparse
import importlib.util
from pathlib import Path
import sys

spec = importlib.util.spec_from_file_location('appimage_update', Path(__file__).with_name('check-appimage-update.py'))
checker = importlib.util.module_from_spec(spec)
spec.loader.exec_module(checker)


def expected_assets():
    names = set()
    for arch in ('x64', 'arm64'):
        for platform, extensions in (
            ('linux', ('.AppImage', '.AppImage.zsync', '.tar.gz')),
            ('windows', ('-setup.exe', '-portable.zip')),
            ('macos', ('.dmg', '.zip')),
        ):
            names.update(f'CyberSnapper-{platform}-{arch}{extension}' for extension in extensions)
    return names


def verify(directory):
    directory = Path(directory)
    expected = expected_assets()
    actual = {path.name for path in directory.iterdir() if path.name != 'SHA256SUMS.txt'}
    checker.require(actual == expected,
                    f'Release assets differ: missing={sorted(expected - actual)}, unexpected={sorted(actual - expected)}')
    for name in sorted(expected):
        path = directory / name
        checker.require(path.is_file() and not path.is_symlink() and path.stat().st_size > 0,
                        f'Missing, empty, or symlinked release asset: {name}')
    for arch in ('x64', 'arm64'):
        # Architecture-neutral validation: each native package job already
        # executes its runtime query and a full offline zsync reconstruction.
        checker.verify_sidecar(directory / f'CyberSnapper-linux-{arch}.AppImage')
    sums = ''.join(f'{checker.digest(directory / name, "sha256")}  {name}\n' for name in sorted(expected))
    (directory / 'SHA256SUMS.txt').write_text(sums)
    print(f'Verified {len(expected)} release assets and wrote SHA256SUMS.txt')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    args = parser.parse_args()
    try:
        verify(args.directory)
    except (OSError, ValueError) as error:
        print(f'Release asset verification failed: {error}', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
