import { access, readFile } from 'node:fs/promises';
import { dirname, isAbsolute, relative, resolve } from 'node:path';
import { spawn, type ChildProcess } from 'node:child_process';
import { chromium, firefox, webkit, type BrowserType } from 'playwright';
import type { BrowserEngine } from './protocol.js';

export const browserTypes: Record<BrowserEngine, BrowserType> = { chromium, firefox, webkit };

export interface InstallProgressContext {
  component: string;
}

export interface BrowserDiagnostics {
  code: string;
  message: string;
  missingLibraries: string[];
  platform: string;
  distribution?: string;
  dependencyCommand?: string;
}

export interface BrowserVerification {
  engine: BrowserEngine;
  downloaded: boolean;
  installed: boolean;
  ready: boolean;
  state: 'ready' | 'not_downloaded' | 'downloaded_unavailable';
  executablePath: string;
  message: string;
  diagnostics?: BrowserDiagnostics;
}

interface InstallEvent {
  protocolVersion: 1;
  type: 'browser_install_progress' | 'browser_install_result';
  engine: BrowserEngine;
  [key: string]: unknown;
}

const ansiPattern = new RegExp('[\\u001B\\u009B][[\\]()#;?]*(?:(?:(?:[a-zA-Z\\d]*(?:;[-a-zA-Z\\d\\/#&.:=?%@~_]+)*)?\\u0007)|(?:(?:\\d{1,4}(?:;\\d{0,4})*)?[\\dA-PR-TZcf-nq-uy=><~]))', 'g');

export function stripAnsi(value: string): string {
  return value.replace(ansiPattern, '');
}

export function parseInstallOutputLine(
  line: string, context: InstallProgressContext,
): { phase: string; component?: string; percent?: number; total?: string; message: string } | undefined {
  const message = stripAnsi(line).trim();
  if (!message) return undefined;
  const downloading = /^Downloading (.+?)(?:\s+\(playwright|\s+from\s+)/i.exec(message);
  if (downloading) context.component = downloading[1].trim();
  const progress = /\|\s*(\d{1,3})%\s+of\s+([\d.]+\s+\S+)/.exec(message);
  if (progress) {
    return {
      phase: 'downloading', component: context.component || 'Browser files',
      percent: Math.min(100, Number(progress[1])), total: progress[2], message,
    };
  }
  if (/downloaded to\s+/i.test(message)) {
    return { phase: 'extracting', component: context.component || 'Browser files', percent: 100, message };
  }
  return {
    phase: downloading ? 'downloading' : 'preparing',
    component: context.component || undefined,
    message,
  };
}

export function parseBrowserLaunchError(message: string, platform = process.platform,
                                        distribution?: string): BrowserDiagnostics {
  const cleaned = stripAnsi(message).trim();
  const missingLibraries = [...new Set(
    [...cleaned.matchAll(/\b(lib[A-Za-z0-9_+.-]+\.so(?:\.[0-9]+)*)\b/g)].map((match) => match[1]),
  )].sort();
  return {
    code: missingLibraries.length ? 'missing_dependencies' : 'launch_failed',
    message: cleaned || 'The browser could not be launched.',
    missingLibraries,
    platform,
    ...(distribution ? { distribution } : {}),
  };
}

export async function resolvePlaywrightCli(): Promise<string> {
  const packageJson = require.resolve('playwright/package.json') as string;
  const packageRoot = dirname(packageJson);
  const manifest = JSON.parse(await readFile(packageJson, 'utf8')) as {
    bin?: string | Record<string, string>;
  };
  const declared = typeof manifest.bin === 'string' ? manifest.bin : manifest.bin?.playwright;
  if (!declared) throw new Error('The packaged Playwright manifest has no playwright CLI entry.');
  const cli = resolve(packageRoot, declared);
  const fromRoot = relative(packageRoot, cli);
  if (fromRoot.startsWith('..') || isAbsolute(fromRoot)) {
    throw new Error('The packaged Playwright CLI entry points outside its package.');
  }
  await access(cli);
  return cli;
}

async function distributionInfo(): Promise<{ name?: string; supported: boolean }> {
  if (process.platform !== 'linux') return { supported: false };
  try {
    const release = await readFile('/etc/os-release', 'utf8');
    const values = new Map<string, string>();
    for (const line of release.split('\n')) {
      const match = /^([A-Z_]+)=(.*)$/.exec(line);
      if (match) values.set(match[1], match[2].replace(/^['"]|['"]$/g, ''));
    }
    const id = values.get('ID')?.toLowerCase();
    return {
      name: values.get('PRETTY_NAME') || id,
      supported: id === 'ubuntu' || id === 'debian',
    };
  } catch {
    return { supported: false };
  }
}

function shellQuote(value: string): string {
  if (process.platform === 'win32') return `"${value.replaceAll('"', '\\"')}"`;
  return `'${value.replaceAll("'", "'\\''")}'`;
}

export async function verifyBrowser(engine: BrowserEngine): Promise<BrowserVerification> {
  const type = browserTypes[engine];
  const executablePath = type.executablePath();
  const downloaded = await access(executablePath).then(() => true).catch(() => false);
  if (!downloaded) {
    return {
      engine, downloaded: false, installed: false, ready: false,
      state: 'not_downloaded', executablePath,
      message: `${engine} is not downloaded.`,
    };
  }
  try {
    const browser = await type.launch({ headless: true });
    await browser.close();
    return {
      engine, downloaded: true, installed: true, ready: true,
      state: 'ready', executablePath,
      message: `${engine} launched successfully.`,
    };
  } catch (error) {
    const distro = await distributionInfo();
    const message = error instanceof Error ? error.message : String(error);
    const diagnostics = parseBrowserLaunchError(message, process.platform, distro.name);
    if (distro.supported) {
      try {
        const cli = await resolvePlaywrightCli();
        diagnostics.dependencyCommand = `sudo ${shellQuote(process.execPath)} ${shellQuote(cli)} install-deps ${engine}`;
      } catch {
        // The missing CLI is already reported by the installation path; launch
        // diagnostics remain useful without a copyable dependency command.
      }
    }
    return {
      engine, downloaded: true, installed: true, ready: false,
      state: 'downloaded_unavailable', executablePath,
      message: `Downloaded, but ${engine} could not launch.`, diagnostics,
    };
  }
}

export async function browserStatus(): Promise<void> {
  const status: Record<string, { executablePath: string; installed: boolean; downloaded: boolean }> = {};
  for (const [name, type] of Object.entries(browserTypes)) {
    const executablePath = type.executablePath();
    const downloaded = await access(executablePath).then(() => true).catch(() => false);
    status[name] = { executablePath, installed: downloaded, downloaded };
  }
  process.stdout.write(`${JSON.stringify({ browsers: status })}\n`);
}

function emitInstall(event: InstallEvent): void {
  process.stdout.write(`${JSON.stringify(event)}\n`);
}

function splitProcessOutput(
  engine: BrowserEngine, context: InstallProgressContext, started: number,
  pending: { value: string }, chunk: Buffer,
): void {
  pending.value += chunk.toString('utf8').replaceAll('\r', '\n');
  const lines = pending.value.split('\n');
  pending.value = lines.pop() ?? '';
  for (const line of lines) {
    const parsed = parseInstallOutputLine(line, context);
    if (!parsed) continue;
    emitInstall({
      protocolVersion: 1, type: 'browser_install_progress', engine,
      elapsedMs: Date.now() - started, ...parsed,
    });
  }
}

export async function installBrowser(engineName: string, force = false): Promise<number> {
  if (!(engineName in browserTypes)) throw new Error(`Unsupported browser engine: ${engineName}`);
  const engine = engineName as BrowserEngine;
  const started = Date.now();
  const context: InstallProgressContext = { component: '' };
  emitInstall({
    protocolVersion: 1, type: 'browser_install_progress', engine,
    phase: 'preparing', message: `Preparing ${engine} installation…`, elapsedMs: 0,
  });

  let cli: string;
  try {
    cli = await resolvePlaywrightCli();
  } catch (error) {
    const message = error instanceof Error ? error.message : String(error);
    emitInstall({
      protocolVersion: 1, type: 'browser_install_result', engine,
      state: 'failed', downloaded: false, ready: false, ok: false, message,
      diagnostics: parseBrowserLaunchError(message), elapsedMs: Date.now() - started,
    });
    return 1;
  }

  const args = [cli, 'install', ...(force ? ['--force'] : []), engine];
  let child: ChildProcess | undefined;
  let cancelled = false;
  const logs: string[] = [];
  const stdout = { value: '' };
  const stderr = { value: '' };
  const cancelHandler = (value: string): void => {
    if (!/"command"\s*:\s*"cancel"|^\s*cancel\s*$/im.test(value) || !child) return;
    cancelled = true;
    child.kill('SIGTERM');
    const timer = setTimeout(() => child?.kill('SIGKILL'), 3000);
    timer.unref();
  };
  const exitCode = await new Promise<number>((resolveExit) => {
    child = spawn(process.execPath, args, {
      stdio: ['pipe', 'pipe', 'pipe'],
      env: {
        ...process.env,
        PLAYWRIGHT_BROWSERS_PATH:
          process.env.CYBERSNAPPER_BROWSER_CACHE || process.env.PLAYWRIGHT_BROWSERS_PATH || '',
      },
    });
    const consume = (pending: { value: string }, chunk: Buffer): void => {
      const clean = stripAnsi(chunk.toString('utf8'));
      logs.push(...clean.replaceAll('\r', '\n').split('\n').filter(Boolean));
      if (logs.length > 200) logs.splice(0, logs.length - 200);
      splitProcessOutput(engine, context, started, pending, chunk);
    };
    child.stdout?.on('data', (chunk: Buffer) => consume(stdout, chunk));
    child.stderr?.on('data', (chunk: Buffer) => consume(stderr, chunk));
    child.once('error', (error) => {
      logs.push(error.message);
      resolveExit(1);
    });
    child.once('close', (code) => resolveExit(code ?? 1));

    process.stdin.setEncoding('utf8');
    process.stdin.on('data', cancelHandler);
  });
  process.stdin.removeListener('data', cancelHandler);
  process.stdin.pause();

  for (const pending of [stdout, stderr]) {
    if (pending.value.trim()) {
      const parsed = parseInstallOutputLine(pending.value, context);
      if (parsed) emitInstall({
        protocolVersion: 1, type: 'browser_install_progress', engine,
        elapsedMs: Date.now() - started, ...parsed,
      });
    }
  }
  if (cancelled) {
    emitInstall({
      protocolVersion: 1, type: 'browser_install_result', engine,
      state: 'cancelled', downloaded: false, ready: false, ok: false,
      message: `${engine} installation was cancelled.`, logs,
      elapsedMs: Date.now() - started,
    });
    return 2;
  }
  if (exitCode !== 0) {
    const message = logs.at(-1) || `Playwright exited with code ${exitCode}.`;
    emitInstall({
      protocolVersion: 1, type: 'browser_install_result', engine,
      state: 'failed', downloaded: false, ready: false, ok: false,
      message, logs, diagnostics: parseBrowserLaunchError(logs.join('\n')),
      exitCode, elapsedMs: Date.now() - started,
    });
    return exitCode;
  }

  emitInstall({
    protocolVersion: 1, type: 'browser_install_progress', engine,
    phase: 'verifying', message: `Verifying that ${engine} can launch…`,
    elapsedMs: Date.now() - started,
  });
  const verification = await verifyBrowser(engine);
  emitInstall({
    protocolVersion: 1, type: 'browser_install_result',
    ...verification, ok: verification.ready, logs,
    elapsedMs: Date.now() - started,
  });
  return verification.ready ? 0 : 3;
}

export async function verifyBrowserCommand(engineName: string): Promise<number> {
  if (!(engineName in browserTypes)) throw new Error(`Unsupported browser engine: ${engineName}`);
  const engine = engineName as BrowserEngine;
  emitInstall({
    protocolVersion: 1, type: 'browser_install_progress', engine,
    phase: 'verifying', message: `Checking whether ${engine} can launch…`, elapsedMs: 0,
  });
  const verification = await verifyBrowser(engine);
  emitInstall({
    protocolVersion: 1, type: 'browser_install_result',
    ...verification, ok: verification.ready, elapsedMs: 0,
  });
  return verification.ready ? 0 : verification.downloaded ? 3 : 1;
}
