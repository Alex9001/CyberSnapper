#!/usr/bin/env python3
"""Stage checked, attested packages on a draft; never publish or replace assets."""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import subprocess
import sys
from urllib.parse import quote

spec = importlib.util.spec_from_file_location('release_assets', Path(__file__).with_name('check-release-assets.py'))
checker = importlib.util.module_from_spec(spec)
spec.loader.exec_module(checker)


def require(condition, message):
    if not condition:
        raise ValueError(message)


def gh(*args):
    return subprocess.run(['gh', *args], check=True, text=True, capture_output=True).stdout


def api(path):
    return json.loads(gh('api', path))


def manifest(directory):
    expected = checker.expected_assets() | {'SHA256SUMS.txt'}
    require({path.name for path in directory.iterdir()} == expected, 'Incomplete or unexpected local release assets')
    result = {}
    for name in sorted(expected):
        path = directory / name
        require(path.is_file() and not path.is_symlink() and path.stat().st_size > 0,
                f'Missing, empty, or symlinked asset: {name}')
        digest = hashlib.sha256()
        with path.open('rb') as source:
            for chunk in iter(lambda: source.read(1024 * 1024), b''):
                digest.update(chunk)
        result[name] = {'size': path.stat().st_size, 'digest': 'sha256:' + digest.hexdigest()}
    expected_sums = ''.join(f'{result[name]["digest"][7:]}  {name}\n'
                            for name in sorted(checker.expected_assets()))
    require((directory / 'SHA256SUMS.txt').read_text() == expected_sums,
            'SHA256SUMS.txt does not match the exact local package bytes')
    for arch in ('x64', 'arm64'):
        checker.checker.verify_sidecar(directory / f'CyberSnapper-linux-{arch}.AppImage')
    return result


def tag_commit(repo, tag):
    target = api(f'repos/{repo}/git/ref/tags/{quote(tag, safe="")}')['object']
    for _ in range(10):
        if target['type'] == 'commit':
            return target['sha']
        require(target['type'] == 'tag', 'Release tag does not identify a commit')
        target = api(f'repos/{repo}/git/tags/{target["sha"]}')['object']
    raise ValueError('Too many nested annotated tags')


def list_api(path):
    # JSON Lines works with older gh versions too; do not depend on --slurp.
    return [json.loads(line) for line in gh('api', '--paginate', path, '--jq', '.[] | @json').splitlines() if line]


def find_release(repo, tag):
    # List includes drafts with this authenticated token. Do not treat a failed
    # lookup as absence or silently fall back after any API/authentication error.
    releases = list_api(f'repos/{repo}/releases?per_page=100')
    matches = [release for release in releases if release['tag_name'] == tag]
    require(len(matches) <= 1, 'Multiple releases refer to this tag')
    return matches[0] if matches else None


def check_draft(release, tag):
    require(release['tag_name'] == tag, 'Release tag changed')
    require(release.get('draft') is True and not release.get('published_at'),
            'Refusing to modify a published release')
    require(bool(release.get('prerelease')) == ('-' in tag), 'Draft prerelease status does not match the tag')


def check_remote(assets, expected, complete=False):
    names = [asset['name'] for asset in assets]
    require(len(names) == len(set(names)), 'Duplicate remote asset names')
    require(set(names) <= set(expected), 'Unexpected remote release assets')
    for asset in assets:
        name = asset['name']
        require(asset.get('state') == 'uploaded' and
                asset.get('size') == expected[name]['size'] and
                asset.get('digest') == expected[name]['digest'],
                f'Conflicting or incomplete remote asset: {name}; existing assets are never replaced')
    if complete:
        require(set(names) == set(expected), 'Draft does not contain the complete verified asset set')
    return set(names)


def preflight(directory, tag, commit, repo):
    require(re.fullmatch(r'[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+', repo or ''), 'GH_REPO must name owner/repository')
    require(re.fullmatch(r'v[0-9]+\.[0-9]+\.[0-9]+(?:-[A-Za-z0-9.-]+)?', tag), 'Invalid release tag')
    require(re.fullmatch(r'[0-9a-f]{40}', commit), 'Expected an exact release commit SHA')
    # The workflow definition, checkout and provenance must all use the same tag.
    require(os.environ.get('GITHUB_REF') == f'refs/tags/{tag}', 'Dispatch the workflow at the exact release tag')
    require(os.environ.get('GITHUB_SHA') == commit, 'Workflow commit does not match the expected release commit')
    require(gh('api', f'repos/{repo}/commits/{commit}', '--jq', '.sha').strip() == commit,
            'Release commit is not available in the repository')
    require(tag_commit(repo, tag) == commit, 'Release tag does not point to the built commit')
    version = json.loads(Path('package.json').read_text())['version']
    require(tag == f'v{version}', 'Release tag does not match the checked-out source version')
    require(subprocess.run(['git', 'rev-parse', 'HEAD'], check=True, text=True,
                           capture_output=True).stdout.strip() == commit,
            'Checked-out source is not the workflow commit')
    expected = manifest(directory)
    release = find_release(repo, tag)
    if release is not None:
        check_draft(release, tag)
        check_remote(list_api(f'repos/{repo}/releases/{release["id"]}/assets?per_page=100'), expected)
    return expected, version, release


def stage(directory, tag, commit, repo):
    expected, version, release = preflight(directory, tag, commit, repo)
    if release is None:
        notes = Path('docs/releases') / f'{tag}.md'
        require(notes.is_file(), 'Missing committed release notes')
        flags = ['--prerelease'] if '-' in version else []
        gh('release', 'create', tag, '--repo', repo, '--verify-tag', '--draft',
           '--title', f'CyberSnapper {version}', '--notes-file', str(notes), *flags)
        release = find_release(repo, tag)
        require(release is not None, 'Created draft could not be read back')
    check_draft(release, tag)
    release_id = release['id']
    for name in sorted(expected):
        # Re-read before every upload and never use --clobber or delete an asset.
        release = api(f'repos/{repo}/releases/{release_id}')
        check_draft(release, tag)
        present = check_remote(list_api(f'repos/{repo}/releases/{release_id}/assets?per_page=100'), expected)
        if name not in present:
            gh('release', 'upload', tag, str(directory / name), '--repo', repo)
    release = api(f'repos/{repo}/releases/{release_id}')
    check_draft(release, tag)
    check_remote(list_api(f'repos/{repo}/releases/{release_id}/assets?per_page=100'), expected, complete=True)
    require(tag_commit(repo, tag) == commit, 'Release tag moved while staging')
    print(f'Verified draft {tag}: {len(expected)} assets match {commit}. Publication remains an explicit maintainer action.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    parser.add_argument('--tag', required=True)
    parser.add_argument('--commit', required=True)
    parser.add_argument('--check-only', action='store_true', help='Validate without creating or uploading anything')
    args = parser.parse_args()
    try:
        if args.check_only:
            preflight(args.directory, args.tag, args.commit, os.environ.get('GH_REPO'))
            print('Exact release tag, source, local assets, and draft target verified.')
        else:
            stage(args.directory, args.tag, args.commit, os.environ.get('GH_REPO'))
    except (OSError, ValueError, KeyError, subprocess.CalledProcessError) as error:
        print(f'Draft staging failed: {error}', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
