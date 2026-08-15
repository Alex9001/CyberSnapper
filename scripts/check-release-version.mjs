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
const cmakeMatch = cmakeLists.match(/project\s*\(\s*CyberSnapper\s+VERSION\s+([^\s)]+)/i);
const cmakeVersion = cmakeMatch?.[1];

if (typeof packageVersion !== 'string' || packageVersion.length === 0) {
  fail('package.json must contain a non-empty string version.');
} else if (!cmakeVersion) {
  fail('CMakeLists.txt must declare project(CyberSnapper VERSION <version> ...).');
} else if (packageVersion !== cmakeVersion) {
  fail(`package.json is ${packageVersion}, but CMakeLists.txt is ${cmakeVersion}.`);
} else {
  const expectedTag = `v${packageVersion}`;
  if (requestedTag && requestedTag !== expectedTag) {
    fail(`requested tag is ${requestedTag}, but the source version requires ${expectedTag}.`);
  } else if (requestedTag) {
    console.log(`Release version verified: ${packageVersion} (${requestedTag}).`);
  } else {
    console.log(`Release version verified: ${packageVersion}. No release tag was requested; this is a rehearsal.`);
  }
}
