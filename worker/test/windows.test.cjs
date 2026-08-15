const test = require('node:test');
const assert = require('node:assert/strict');
const { mkdtemp, rm, writeFile } = require('node:fs/promises');
const os = require('node:os');
const path = require('node:path');
const { windowsBinaryIsX64 } = require('../dist/testing.cjs');

function peExecutable(machine) {
  const dos = Buffer.alloc(64);
  dos.writeUInt16LE(0x5a4d, 0); // "MZ"
  dos.writeUInt32LE(64, 0x3c); // e_lfanew -> PE header at offset 64
  const pe = Buffer.alloc(6);
  pe.writeUInt32LE(0x00004550, 0); // "PE\0\0"
  pe.writeUInt16LE(machine, 4);
  return Buffer.concat([dos, pe]);
}

test('windowsBinaryIsX64 detects x64, arm64, and malformed executables', async () => {
  const directory = await mkdtemp(path.join(os.tmpdir(), 'cybersnapper-pe-'));
  try {
    const x64 = path.join(directory, 'x64.exe');
    const arm64 = path.join(directory, 'arm64.exe');
    const truncated = path.join(directory, 'truncated.exe');
    await writeFile(x64, peExecutable(0x8664));
    await writeFile(arm64, peExecutable(0xaa64));
    await writeFile(truncated, Buffer.from([0x4d, 0x5a]));

    assert.equal(await windowsBinaryIsX64(x64), true);
    assert.equal(await windowsBinaryIsX64(arm64), false);
    assert.equal(await windowsBinaryIsX64(truncated), false);
    assert.equal(await windowsBinaryIsX64(path.join(directory, 'missing.exe')), false);
  } finally {
    await rm(directory, { recursive: true, force: true });
  }
});
