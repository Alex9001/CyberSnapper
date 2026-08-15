import { open, type FileHandle } from 'node:fs/promises';

const IMAGE_FILE_MACHINE_AMD64 = 0x8664;

// Reads a Windows portable-executable header and reports whether the binary is
// x64. On Windows arm64, an x64 Chromium runs under emulation, so callers use
// this to decide whether to prefer a native system browser instead.
export async function windowsBinaryIsX64(filePath: string): Promise<boolean> {
  let handle: FileHandle | undefined;
  try {
    handle = await open(filePath, 'r');
    const dos = Buffer.alloc(64);
    if ((await handle.read(dos, 0, dos.length, 0)).bytesRead < dos.length) return false;
    if (dos.readUInt16LE(0) !== 0x5a4d) return false; // "MZ"
    const peOffset = dos.readUInt32LE(0x3c);
    const header = Buffer.alloc(6);
    if ((await handle.read(header, 0, header.length, peOffset)).bytesRead < header.length) return false;
    if (header.readUInt32LE(0) !== 0x00004550) return false; // "PE\0\0"
    return header.readUInt16LE(4) === IMAGE_FILE_MACHINE_AMD64;
  } catch {
    return false;
  } finally {
    await handle?.close().catch(() => undefined);
  }
}
