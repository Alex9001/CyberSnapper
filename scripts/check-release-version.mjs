#!/usr/bin/env node

import { readFile } from 'node:fs/promises';
import process from 'node:process';

const requestedTag = process.argv[2] ?? '';

function fail(message) {
  console.error(`Release version check failed: ${message}`);
  process.exitCode = 1;
}

const packageJson = JSON.parse(await readFile(new URL('../package.json', import.meta.url), 'utf8'));
const cmakeLists = await readFile(new URL('../CMakeLists.txt', import.meta.url), 'utf8');
const packageVersion = packageJson.version;
const lock = JSON.parse(await readFile(new URL('../package-lock.json', import.meta.url), 'utf8'));
const desktop = await readFile(new URL('../native/packaging/net.cyberbrand.CyberSnapper.desktop', import.meta.url), 'utf8');
const metadata = await readFile(new URL('../native/packaging/net.cyberbrand.CyberSnapper.metainfo.xml', import.meta.url), 'utf8');
const website = await readFile(new URL('../site/index.html', import.meta.url), 'utf8');
const versions = {
  'package-lock.json': lock.version,
  'package-lock.json root package': lock.packages?.['']?.version,
  'desktop AppImage version': desktop.match(/^X-AppImage-Version=(.+)$/m)?.[1],
  'AppStream latest release': metadata.match(/<release\s+version="([^"]+)"/)?.[1],
  'website softwareVersion': website.match(/"softwareVersion":\s*"([^"]+)"/)?.[1],
};
for (const [source, version] of Object.entries(versions)) {
  if (version !== packageVersion) fail(`${source} is ${version}, expected ${packageVersion}.`);
}

const cmakeMatch = cmakeLists.match(/project\s*\(\s*CyberSnapper\s+VERSION\s+([^\s)]+)/i);
const cmakeVersion = cmakeMatch?.[1];

if (typeof packageVersion !== 'string' || packageVersion.length === 0) {
  fail('package.json must contain a non-empty string version.');
} else if (!cmakeVersion) {
  fail('CMakeLists.txt must declare project(CyberSnapper VERSION <version> ...).');
} else if (packageVersion !== cmakeVersion) {
  fail(`package.json is ${packageVersion}, but CMakeLists.txt is ${cmakeVersion}.`);
} else if (!process.exitCode) {
  const expectedTag = `v${packageVersion}`;
  if (requestedTag && requestedTag !== expectedTag) {
    fail(`requested tag is ${requestedTag}, but the source version requires ${expectedTag}.`);
  } else if (requestedTag) {
    console.log(`Release version verified: ${packageVersion} (${requestedTag}).`);
  } else {
    console.log(`Release version verified: ${packageVersion}. No release tag was requested; this is a rehearsal.`);
  }
}
