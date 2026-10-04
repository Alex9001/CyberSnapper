import assert from 'node:assert/strict';
import { mkdtemp, mkdir, writeFile, readdir, rm, symlink } from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';
import { requiredSharpPackages, pruneSharpPackages, loadStaged } from './prune-worker-dependencies.mjs';

const targets = ['linux', 'darwin', 'win32'].flatMap(platform => ['x64', 'arm64'].map(arch => ({ platform, arch, libc: 'glibc' })));
const variants = [...new Set(targets.flatMap(requiredSharpPackages)), 'sharp-linuxmusl-x64',
  'sharp-libvips-linuxmusl-x64', 'sharp-linuxmusl-arm64', 'sharp-libvips-linuxmusl-arm64', 'sharp-wasm32'];

async function fixture(t, names = variants) {
  const root = await mkdtemp(path.join(os.tmpdir(), 'cybersnapper-prune-'));
  t.after(() => rm(root, { recursive: true, force: true }));
  const modules = path.join(root, 'node_modules');
  for (const name of [...names, 'colour']) {
    const directory = path.join(modules, '@img', name);
    await mkdir(directory, { recursive: true });
    await writeFile(path.join(directory, 'package.json'), JSON.stringify({ name: `@img/${name}` }));
  }
  return modules;
}

for (const target of targets) {
  test(`retain only native Sharp dependencies for ${target.platform}-${target.arch}`, async t => {
    const modules = await fixture(t);
    const result = await pruneSharpPackages(modules, target);
    assert.deepEqual((await readdir(path.join(modules, '@img'))).sort(), ['colour', 'sharp-wasm32', ...requiredSharpPackages(target)].sort());
    assert(!result.removed.includes('sharp-wasm32'));
    assert(result.removed.includes('sharp-linuxmusl-x64'));
    assert.deepEqual(await pruneSharpPackages(modules, target), { kept: requiredSharpPackages(target), removed: [] });
  });
}

test('missing native build fails before deleting other packages', async t => {
  const modules = await fixture(t, variants.filter(name => name !== 'sharp-linux-x64'));
  const before = await readdir(path.join(modules, '@img'));
  await assert.rejects(pruneSharpPackages(modules, targets[0]), /Required native package missing/);
  assert.deepEqual(await readdir(path.join(modules, '@img')), before);
});

test('incorrect package identity fails before pruning', async t => {
  const modules = await fixture(t);
  await writeFile(path.join(modules, '@img/sharp-linux-x64/package.json'), JSON.stringify({ name: 'wrong' }));
  await assert.rejects(pruneSharpPackages(modules, targets[0]), /Unexpected package identity/);
  assert((await readdir(path.join(modules, '@img'))).includes('sharp-linuxmusl-x64'));
});

test('symlinked optional package is rejected without following it', async t => {
  const modules = await fixture(t);
  await symlink(path.join(modules, '@img/colour'), path.join(modules, '@img/sharp-foreign'));
  await assert.rejects(pruneSharpPackages(modules, targets[0]), /not a regular directory/);
  assert((await readdir(path.join(modules, '@img'))).includes('colour'));
});

test('unrecognized Sharp helper fails without deleting anything', async t => {
  const modules = await fixture(t, [...variants, 'sharp-new-helper']);
  const before = await readdir(path.join(modules, '@img'));
  await assert.rejects(pruneSharpPackages(modules, targets[0]), /Unrecognized Sharp deployment package/);
  assert.deepEqual(await readdir(path.join(modules, '@img')), before);
});

test('unsupported platforms and musl hosts fail closed', () => {
  for (const target of [{ platform: 'linux', arch: 'x64', libc: 'musl' },
    { platform: 'linux', arch: 'x64' }, { platform: 'linux', arch: 'ia32', libc: 'glibc' },
    { platform: 'freebsd', arch: 'x64' }]) assert.throws(() => requiredSharpPackages(target));
});


test('staged loader cannot fall back to source-tree dependencies', async t => {
  const modules = await fixture(t);
  await writeFile(path.join(modules, '@img/colour/index.js'), 'module.exports = 42;');
  assert.equal(await loadStaged(modules, '@img/colour'), 42);
  const isolated = path.join(modules, '..', 'stage', 'node_modules');
  await mkdir(isolated, { recursive: true });
  await assert.rejects(loadStaged(isolated, '@img/colour'), /outside staged node_modules/);
});
