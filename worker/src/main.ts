import { createInterface } from 'node:readline';
import { access } from 'node:fs/promises';
import { spawn } from 'node:child_process';
import { chromium, firefox, webkit, type BrowserType } from 'playwright';
import { runCaptureJob, type JobRuntime } from './capture.js';
import type { BrowserEngine, CaptureJob, WorkerEvent } from './protocol.js';

let sequence = 0;
let currentJobId = '';
let runtime: JobRuntime | undefined;

function emit(type: string, data: Record<string, unknown> = {}): void {
  const event: WorkerEvent = { protocolVersion: 2, sequence: ++sequence,
    timestamp: new Date().toISOString(), type, jobId: currentJobId, ...data };
  process.stdout.write(`${JSON.stringify(event)}\n`);
}

async function cancel(): Promise<void> {
  if (!runtime) return;
  runtime.cancelled = true;
  await Promise.all([...runtime.browsers].map((browser) => browser.close().catch(() => undefined)));
}

async function stdioMode(): Promise<void> {
  const lines = createInterface({ input: process.stdin, crlfDelay: Infinity });
  for await (const line of lines) {
    if (!line.trim()) continue;
    let command: { protocolVersion?: number; command?: string; job?: CaptureJob; jobId?: string };
    try { command = JSON.parse(line) as typeof command; }
    catch (error) { emit('worker_protocol_error', { message: error instanceof Error ? error.message : String(error) }); continue; }
    if (command.protocolVersion !== 2) { emit('worker_protocol_error', { message: 'Unsupported protocol version' }); continue; }
    if (command.command === 'cancel') { await cancel(); continue; }
    if (command.command !== 'run' || !command.job) { emit('worker_protocol_error', { message: 'Expected a run command with a job' }); continue; }
    if (runtime) { emit('worker_protocol_error', { message: 'Worker already has a job' }); continue; }
    currentJobId = command.job.id;
    runtime = { cancelled: false, browsers: new Set() };
    const heartbeat = setInterval(() => emit('worker_heartbeat'), 10_000);
    heartbeat.unref();
    void runCaptureJob(command.job, runtime, (event) => emit(event.type as string, event))
      .catch((error) => emit(runtime?.cancelled ? 'job_cancelled' : 'job_failed', {
        status: runtime?.cancelled ? 'cancelled' : 'failed',
        message: error instanceof Error ? error.message : String(error),
      }))
      .finally(() => { clearInterval(heartbeat); runtime = undefined; lines.close(); });
  }
}

const types: Record<BrowserEngine, BrowserType> = { chromium, firefox, webkit };

async function browserStatus(): Promise<void> {
  const status: Record<string, { executablePath: string; installed: boolean }> = {};
  for (const [name, type] of Object.entries(types)) {
    const executablePath = type.executablePath();
    status[name] = { executablePath, installed: await access(executablePath).then(() => true).catch(() => false) };
  }
  process.stdout.write(`${JSON.stringify({ browsers: status })}\n`);
}

async function installBrowser(engine: string): Promise<number> {
  if (!(engine in types)) throw new Error(`Unsupported browser engine: ${engine}`);
  const cli = require.resolve('playwright/cli');
  return new Promise((resolve, reject) => {
    const child = spawn(process.execPath, [cli, 'install', engine], { stdio: 'inherit',
      env: { ...process.env, PLAYWRIGHT_BROWSERS_PATH: process.env.CYBERSNAPPER_BROWSER_CACHE || process.env.PLAYWRIGHT_BROWSERS_PATH || '' } });
    child.on('error', reject);
    child.on('close', (code) => resolve(code ?? 1));
  });
}

async function main(): Promise<void> {
  const args = process.argv.slice(2);
  if (args.includes('--stdio')) return stdioMode();
  if (args.includes('--browsers')) return browserStatus();
  const installIndex = args.indexOf('--install');
  if (installIndex >= 0) process.exitCode = await installBrowser(args[installIndex + 1] ?? 'chromium');
  else throw new Error('Use --stdio, --browsers, or --install <engine>');
}

process.on('SIGINT', () => { void cancel(); });
process.on('SIGTERM', () => { void cancel(); });
main().catch((error) => { process.stderr.write(`${error instanceof Error ? error.stack ?? error.message : String(error)}\n`); process.exitCode = 1; });
