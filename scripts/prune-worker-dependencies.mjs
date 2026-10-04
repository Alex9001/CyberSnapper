#!/usr/bin/env node
// Deployment trees need only their native Sharp build, even when npm retains
// optional packages for another CPU, operating system, or Linux libc.
import { lstat, readFile, readdir, realpath, rm } from 'node:fs/promises';
import { createRequire } from 'node:module';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

export function requiredSharpPackages({ platform, arch, libc }) {
  if (!['linux', 'darwin', 'win32'].includes(platform) || !['x64', 'arm64'].includes(arch)) {
    throw new Error(`Unsupported release platform: ${platform}-${arch}`);
  }
  if (platform === 'linux' && libc !== 'glibc') {
    throw new Error('Linux releases require a glibc runtime');
  }
  const target = `${platform}-${arch}`;
  return [`sharp-${target}`, ...(platform === 'win32' ? [] : [`sharp-libvips-${target}`])];
}

export async function pruneSharpPackages(modulesRoot, target) {
  const scope = path.resolve(modulesRoot, '@img');
  const required = requiredSharpPackages(target);
  if ((await lstat(scope)).isSymbolicLink()) throw new Error('Sharp package scope must not be a symlink');
  const entries = await readdir(scope, { withFileTypes: true });
  const variants = entries.filter(entry => entry.name.startsWith('sharp-'));
  // Complete all validation before removing anything. Never follow a package
  // symlink out of the install tree or silently substitute a WASM fallback.
  for (const entry of variants) {
    if (!entry.isDirectory() || entry.isSymbolicLink()) {
      throw new Error(`Sharp package is not a regular directory: ${entry.name}`);
    }
  }
  for (const name of required) {
    if (!variants.some(entry => entry.name === name)) throw new Error(`Required native package missing: @img/${name}`);
    const manifest = JSON.parse(await readFile(path.join(scope, name, 'package.json'), 'utf8'));
    if (manifest.name !== `@img/${name}`) throw new Error(`Unexpected package identity: ${name}`);
  }
  const allowed = [...required, 'sharp-wasm32'];
  const removed = variants.map(entry => entry.name).filter(name => !allowed.includes(name)).sort();
  for (const name of removed) {
    if (!/^sharp-(?:libvips-)?(?:linux(?:musl)?|darwin|win32|freebsd|webcontainers)-[a-z0-9]+$/.test(name)) {
      throw new Error(`Unrecognized Sharp deployment package: ${name}`);
    }
  }
  for (const name of removed) await rm(path.join(scope, name), { recursive: true });
  return { kept: required, removed };
}

export async function loadStaged(modulesRoot, specifier) {
  const root = await realpath(modulesRoot);
  const require = createRequire(path.join(root, '..', 'verify-sharp.cjs'));
  const entry = await realpath(require.resolve(specifier));
  const relative = path.relative(root, entry);
  if (relative === '..' || relative.startsWith('..' + path.sep) || path.isAbsolute(relative)) {
    throw new Error(`Dependency resolved outside staged node_modules: ${specifier}`);
  }
  return require(entry);
}

async function main() {
  if (process.argv.length !== 3) throw new Error('usage: prune-worker-dependencies.mjs <staged-node_modules>');
  const modulesRoot = path.resolve(process.argv[2]);
  const sourceModules = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..', 'node_modules');
  if (modulesRoot === sourceModules) throw new Error('Refusing to prune source-tree dependencies; pass a deployment staging tree');
  const target = { platform: process.platform, arch: process.arch,
    libc: process.platform === 'linux' ? (await loadStaged(modulesRoot, 'detect-libc')).familySync() : undefined };
  const result = await pruneSharpPackages(modulesRoot, target);
  // A WASM fallback must not conceal a broken native dependency set.
  await loadStaged(modulesRoot, `@img/sharp-${target.platform}-${target.arch}/sharp.node`);
  const sharp = await loadStaged(modulesRoot, 'sharp');
  for (const format of ['png', 'avif']) {
    const bytes = await sharp({ create: { width: 4, height: 4, channels: 4, background: '#24578fff' } })
      .toFormat(format).toBuffer();
    const metadata = await sharp(bytes).metadata();
    if (metadata.width !== 4 || metadata.height !== 4 || metadata.format !== (format === 'avif' ? 'heif' : 'png')) {
      throw new Error(`Staged Sharp ${format} round-trip failed`);
    }
  }
  console.log(`Verified staged native Sharp for ${target.platform}-${target.arch}: ${JSON.stringify(result)}`);
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  main().catch(error => { console.error(error.message); process.exitCode = 1; });
}
