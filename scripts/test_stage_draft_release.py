"""Offline regression tests for non-destructive, draft-only release staging."""
import copy
import hashlib
import importlib.util
import json
import os
import re
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('stage_draft', ROOT / 'scripts/stage-draft-release.py')
stager = importlib.util.module_from_spec(spec)
spec.loader.exec_module(stager)
SHA = 'a' * 40
TAG = 'v' + json.loads((ROOT / 'package.json').read_text())['version']
REPO = 'example/CyberSnapper'
DRAFT = {'id': 42, 'tag_name': TAG, 'draft': True, 'published_at': None}


def remote_assets(expected):
    return [dict(name=name, state='uploaded', **metadata) for name, metadata in expected.items()]


class ManifestTest(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.directory = Path(temporary.name)
        for name in stager.checker.expected_assets():
            (self.directory / name).write_bytes(b'release fixture')
        for arch in ('x64', 'arm64'):
            image = self.directory / f'CyberSnapper-linux-{arch}.AppImage'
            fields = {'zsync': '0.6.2', 'Filename': image.name, 'URL': image.name,
                      'Length': str(image.stat().st_size), 'SHA-1': hashlib.sha1(image.read_bytes()).hexdigest(),
                      'Blocksize': '2048', 'Hash-Lengths': '1,2,4'}
            image.with_name(image.name + '.zsync').write_bytes(
                ('\n'.join(f'{key}: {value}' for key, value in fields.items()) + '\n\n').encode() + b'123456')
        stager.checker.verify(self.directory)

    def test_all_fifteen_exact_assets(self):
        result = stager.manifest(self.directory)
        self.assertEqual(len(result), 15)
        self.assertIn('SHA256SUMS.txt', result)
        self.assertTrue(all(x['digest'].startswith('sha256:') for x in result.values()))

    def test_missing_sidecar(self):
        (self.directory / 'CyberSnapper-linux-x64.AppImage.zsync').unlink()
        with self.assertRaisesRegex(ValueError, 'Incomplete'):
            stager.manifest(self.directory)

    def test_extra_file(self):
        (self.directory / 'log.txt').write_text('not a release asset')
        with self.assertRaisesRegex(ValueError, 'unexpected'):
            stager.manifest(self.directory)

    def test_stale_checksums_are_not_rewritten(self):
        sums = self.directory / 'SHA256SUMS.txt'
        sums.write_text('invalid\n')
        with self.assertRaisesRegex(ValueError, 'exact local package bytes'):
            stager.manifest(self.directory)
        self.assertEqual(sums.read_text(), 'invalid\n')

    def test_checksum_symlink_rejected(self):
        sums = self.directory / 'SHA256SUMS.txt'
        original = self.directory.parent / (self.directory.name + '-sums')
        original.write_text(sums.read_text())
        self.addCleanup(original.unlink)
        sums.unlink()
        sums.symlink_to(original)
        with self.assertRaisesRegex(ValueError, 'symlinked'):
            stager.manifest(self.directory)


class RemoteAssetsTest(unittest.TestCase):
    def setUp(self):
        self.expected = {'asset': {'size': 3, 'digest': 'sha256:' + 'a' * 64}}

    def test_complete_matching_assets(self):
        self.assertEqual(stager.check_remote(remote_assets(self.expected), self.expected, True), {'asset'})

    def test_partial_matching_assets_allowed_only_during_upload(self):
        self.assertEqual(stager.check_remote([], self.expected), set())
        with self.assertRaisesRegex(ValueError, 'complete'):
            stager.check_remote([], self.expected, True)

    def test_reject_duplicate_names(self):
        with self.assertRaisesRegex(ValueError, 'Duplicate'):
            stager.check_remote(remote_assets(self.expected) * 2, self.expected)

    def test_reject_unexpected_names(self):
        with self.assertRaisesRegex(ValueError, 'Unexpected'):
            stager.check_remote([{'name': 'surprise'}], self.expected)

    def test_reject_starter_missing_digest_wrong_size_and_wrong_digest(self):
        for change in ({'state': 'starter'}, {'digest': None}, {'size': 0}, {'digest': 'sha256:bad'}):
            with self.subTest(change=change):
                assets = remote_assets(self.expected)
                assets[0].update(change)
                with self.assertRaisesRegex(ValueError, 'never replaced'):
                    stager.check_remote(assets, self.expected)


class IdentityTest(unittest.TestCase):
    def setUp(self):
        self.env = patch.dict(os.environ, {'GITHUB_REF': f'refs/tags/{TAG}', 'GITHUB_SHA': SHA})
        self.env.start()
        self.addCleanup(self.env.stop)

    def preflight(self):
        with patch.object(stager, 'gh', return_value=SHA + '\n'), \
             patch.object(stager, 'tag_commit', return_value=SHA), \
             patch.object(stager, 'manifest', return_value={}), \
             patch.object(stager, 'find_release', return_value=None), \
             patch.object(stager.subprocess, 'run', return_value=subprocess.CompletedProcess([], 0, SHA + '\n')):
            return stager.preflight(Path('unused'), TAG, SHA, REPO)

    def test_matching_exact_tag_and_commit(self):
        self.assertEqual(self.preflight()[2], None)

    def test_branch_dispatch_rejected(self):
        os.environ['GITHUB_REF'] = 'refs/heads/master'
        with self.assertRaisesRegex(ValueError, 'exact release tag'):
            self.preflight()

    def test_wrong_workflow_commit_rejected(self):
        os.environ['GITHUB_SHA'] = 'b' * 40
        with self.assertRaisesRegex(ValueError, 'Workflow commit'):
            self.preflight()

    def test_lightweight_tag(self):
        with patch.object(stager, 'api', return_value={'object': {'type': 'commit', 'sha': SHA}}):
            self.assertEqual(stager.tag_commit(REPO, TAG), SHA)

    def test_annotated_tag(self):
        with patch.object(stager, 'api', side_effect=[{'object': {'type': 'tag', 'sha': 'b' * 40}},
                                                    {'object': {'type': 'commit', 'sha': SHA}}]):
            self.assertEqual(stager.tag_commit(REPO, TAG), SHA)

    def test_tag_tree_rejected(self):
        with patch.object(stager, 'api', return_value={'object': {'type': 'tree', 'sha': SHA}}):
            with self.assertRaisesRegex(ValueError, 'does not identify a commit'):
                stager.tag_commit(REPO, TAG)

    def test_paginated_json_lines(self):
        with patch.object(stager, 'gh', return_value='{"name":"one"}\n{"name":"two"}\n') as cli:
            self.assertEqual(len(stager.list_api('path')), 2)
            self.assertIn('--paginate', cli.call_args.args)

    def test_release_lookup_failure_is_not_absence(self):
        with patch.object(stager, 'list_api', side_effect=subprocess.CalledProcessError(1, ['gh'])):
            with self.assertRaises(subprocess.CalledProcessError):
                stager.find_release(REPO, TAG)


class StagingTest(unittest.TestCase):
    def setUp(self):
        self.expected = {'one': {'size': 1, 'digest': 'sha256:1'}, 'two': {'size': 2, 'digest': 'sha256:2'}}
        self.assets = []
        self.release = copy.deepcopy(DRAFT)
        self.commands = []

    def cli(self, *args):
        self.commands.append(args)
        if args[:2] == ('release', 'upload'):
            name = Path(args[3]).name
            self.assets.append(dict(name=name, state='uploaded', **self.expected[name]))
        return ''

    def stage(self, release=None):
        with patch.object(stager, 'preflight', return_value=(self.expected, TAG[1:], release or self.release)), \
             patch.object(stager, 'gh', side_effect=self.cli), \
             patch.object(stager, 'api', side_effect=lambda _: copy.deepcopy(self.release)), \
             patch.object(stager, 'list_api', side_effect=lambda _: copy.deepcopy(self.assets)), \
             patch.object(stager, 'tag_commit', return_value=SHA):
            stager.stage(Path('artifacts'), TAG, SHA, REPO)

    def test_uploads_missing_assets_without_clobber_or_publication(self):
        self.stage()
        self.assertEqual({a['name'] for a in self.assets}, set(self.expected))
        self.assertEqual(len(self.commands), 2)
        self.assertTrue(all(command[:2] == ('release', 'upload') for command in self.commands))
        self.assertNotIn('--clobber', str(self.commands))

    def test_identical_retry_does_not_upload(self):
        self.assets = remote_assets(self.expected)
        self.stage()
        self.assertEqual(self.commands, [])

    def test_published_release_is_never_modified(self):
        self.release.update(draft=False, published_at='2026-10-04T12:00:00Z')
        with self.assertRaisesRegex(ValueError, 'published'):
            self.stage()
        self.assertEqual(self.commands, [])

    def test_publish_during_staging_stops_further_uploads(self):
        original = self.cli
        def publishing_cli(*args):
            result = original(*args)
            self.release.update(draft=False, published_at='2026-10-04T12:00:00Z')
            return result
        self.cli = publishing_cli
        with self.assertRaisesRegex(ValueError, 'published'):
            self.stage()
        self.assertEqual(len(self.commands), 1)

    def test_conflicting_existing_asset_is_preserved(self):
        self.assets = [{'name': 'one', 'state': 'uploaded', 'size': 10, 'digest': 'sha256:other'}]
        before = copy.deepcopy(self.assets)
        with self.assertRaisesRegex(ValueError, 'never replaced'):
            self.stage()
        self.assertEqual(self.commands, [])
        self.assertEqual(self.assets, before)


    def test_creates_only_a_draft_and_verifies_readback(self):
        with patch.object(stager, 'preflight', return_value=(self.expected, TAG[1:], None)), \
             patch.object(stager, 'find_release', return_value=self.release) as lookup, \
             patch.object(stager, 'gh', side_effect=self.cli), \
             patch.object(stager, 'api', side_effect=lambda _: copy.deepcopy(self.release)), \
             patch.object(stager, 'list_api', side_effect=lambda _: copy.deepcopy(self.assets)), \
             patch.object(stager, 'tag_commit', return_value=SHA):
            stager.stage(Path('artifacts'), TAG, SHA, REPO)
        create = self.commands[0]
        self.assertEqual(create[:3], ('release', 'create', TAG))
        for flag in ('--draft', '--verify-tag', '--notes-file'):
            self.assertIn(flag, create)
        self.assertNotIn('--latest', create)
        lookup.assert_called_once_with(REPO, TAG)
        self.assertEqual(len(self.assets), 2)

    def test_failed_create_readback_stops_before_upload(self):
        with patch.object(stager, 'preflight', return_value=(self.expected, TAG[1:], None)), \
             patch.object(stager, 'find_release', return_value=None), \
             patch.object(stager, 'gh', side_effect=self.cli):
            with self.assertRaisesRegex(ValueError, 'could not be read back'):
                stager.stage(Path('artifacts'), TAG, SHA, REPO)
        self.assertEqual(len(self.commands), 1)
        self.assertEqual(self.assets, [])

    def test_missing_uploaded_asset_fails_final_verification(self):
        self.cli = lambda *args: self.commands.append(args) or ''
        with self.assertRaisesRegex(ValueError, 'complete verified asset set'):
            self.stage()

    def test_moved_tag_fails_final_verification(self):
        self.assets = remote_assets(self.expected)
        with patch.object(stager, 'preflight', return_value=(self.expected, TAG[1:], self.release)), \
             patch.object(stager, 'api', return_value=self.release), \
             patch.object(stager, 'list_api', return_value=self.assets), \
             patch.object(stager, 'tag_commit', return_value='b' * 40):
            with self.assertRaisesRegex(ValueError, 'tag moved'):
                stager.stage(Path('artifacts'), TAG, SHA, REPO)

    def test_prerelease_status_must_match(self):
        with self.assertRaisesRegex(ValueError, 'prerelease status'):
            stager.check_draft(dict(DRAFT, tag_name=TAG + '-rc.1'), TAG + '-rc.1')


class WorkflowSafetyTest(unittest.TestCase):
    def setUp(self):
        self.workflow = (ROOT / '.github/workflows/release.yml').read_text()

    def test_only_explicit_dispatch_no_public_release_mutations(self):
        self.assertNotRegex(self.workflow, r'(?m)^  release:')
        self.assertNotIn('--clobber', self.workflow)
        self.assertNotIn('gh release edit', self.workflow)
        self.assertIn('workflow_dispatch:', self.workflow)
        self.assertIn('cancel-in-progress: false', self.workflow)

    def test_all_package_and_validation_gates_retained(self):
        for job in ('browser-matrix', 'linux', 'windows', 'macos', 'appimage-compatibility', 'appimage-catalog', 'publish'):
            self.assertIn(f'\n  {job}:\n', self.workflow)
        self.assertIn('needs: [browser-matrix]', self.workflow)
        self.assertIn('needs: [linux, windows, macos, appimage-compatibility, appimage-catalog]', self.workflow)
        self.assertIn('os: [ubuntu-22.04, ubuntu-24.04]', self.workflow)
        self.assertEqual(len(re.findall(r'(?m)^            arch: x64$', self.workflow)), 3)
        self.assertEqual(len(re.findall(r'(?m)^            arch: arm64$', self.workflow)), 3)

    def test_candidate_checkouts_use_immutable_workflow_sha(self):
        # Seven candidate checkouts; the eighth checkout is a separately pinned upstream catalog.
        self.assertEqual(self.workflow.count('ref: ${{ github.sha }}'), 7)
        self.assertNotIn('ref: ${{ inputs.release_tag', self.workflow)

    def test_draft_preflight_then_attestation_then_staging(self):
        self.assertLess(self.workflow.index('--check-only'), self.workflow.index('- name: Attest package provenance'))
        self.assertLess(self.workflow.index('- name: Attest package provenance'), self.workflow.index('- name: Stage complete verified draft'))
        self.assertIn('id-token: write\n      attestations: write', self.workflow)
        self.assertIn('subject-path: artifacts/*', self.workflow)


if __name__ == '__main__':
    unittest.main()
