import path from 'node:path';
import { access } from 'node:fs/promises';
import type { BrowserEngine, CaptureJob, OutputFormat, Viewport } from './protocol.js';

export function safeSegment(input: string, fallback = 'capture'): string {
  const value = input
    .normalize('NFKD')
    .replace(/[\u0300-\u036f]/g, '')
    .replace(/[<>:"/\\|?*\u0000-\u001f]/g, '-')
    .replace(/\s+/g, '-')
    .replace(/[^A-Za-z0-9._-]+/g, '-')
    .replace(/[-_.]{2,}/g, '-')
    .replace(/^[-_.]+|[-_.]+$/g, '')
    .slice(0, 120);
  const reserved = /^(con|prn|aux|nul|com[1-9]|lpt[1-9])$/i.test(value);
  return !value || reserved ? fallback : value;
}

export function captureName(job: CaptureJob, urlText: string, viewport: Viewport,
                            engine: BrowserEngine, index = 0): { directories: string[]; base: string } {
  const url = new URL(urlText);
  const now = new Date();
  const replacements: Record<string, string> = {
    hostname: safeSegment(url.hostname),
    domain: safeSegment(url.hostname),
    url: safeSegment(`${url.hostname}${url.pathname}`),
    path: safeSegment(url.pathname === '/' ? 'home' : url.pathname),
    slug: safeSegment(`${url.hostname}${url.pathname}`),
    preset: safeSegment(viewport.name),
    width: String(viewport.width),
    height: String(viewport.height),
    engine,
    date: now.toISOString().slice(0, 10),
    time: now.toISOString().slice(11, 19).replaceAll(':', '-'),
    job: job.id.slice(0, 8),
    index: String(index + 1).padStart(2, '0'),
  };
  let rendered = job.profile.namingTemplate || '{hostname}-{preset}';
  rendered = rendered.replace(/\{([a-z]+)\}/gi, (token, key: string) => replacements[key] ?? token);
  const segments = rendered.split('/').map((segment) => safeSegment(segment)).filter(Boolean);
  const base = segments.pop() ?? 'capture';
  return { directories: segments, base };
}

export function captureBaseName(job: CaptureJob, urlText: string, viewport: Viewport,
                                engine: BrowserEngine, index = 0): string {
  return captureName(job, urlText, viewport, engine, index).base;
}

export async function chooseOutputPath(directory: string, base: string, format: OutputFormat,
                                       policy: CaptureJob['profile']['collisionPolicy'],
                                       reserved: Set<string>): Promise<{ absolute: string; skipped: boolean }> {
  const candidate = (suffix = '') => path.join(directory, `${base}${suffix}.${format}`);
  const exists = async (file: string): Promise<boolean> => {
    if (reserved.has(file)) return true;
    try { await access(file); return true; } catch { return false; }
  };
  const initial = candidate();
  if (policy === 'overwrite') { reserved.add(initial); return { absolute: initial, skipped: false }; }
  if (!(await exists(initial))) { reserved.add(initial); return { absolute: initial, skipped: false }; }
  if (policy === 'skip') return { absolute: initial, skipped: true };
  for (let version = 2; version < 10000; version += 1) {
    const versioned = candidate(`-${version}`);
    if (!(await exists(versioned))) { reserved.add(versioned); return { absolute: versioned, skipped: false }; }
  }
  throw new Error('Could not find an available output filename');
}
