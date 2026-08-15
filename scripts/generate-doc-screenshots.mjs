import { execFile, spawn } from 'node:child_process';
import { access, chmod, mkdir, rm, writeFile } from 'node:fs/promises';
import { createRequire } from 'node:module';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { promisify } from 'node:util';
import sharp from 'sharp';

const exec = promisify(execFile);
const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const require = createRequire(import.meta.url);
const runtimeRoot = path.join(root, 'build', 'test-runtime', 'docs-screenshots');
const buildRoot = path.resolve(process.env.CYBERSNAPPER_NATIVE_BUILD || path.join(root, 'build', 'native'));
const requestedOutput = process.argv.indexOf('--output');
const outputRoot = requestedOutput >= 0
  ? path.resolve(process.argv[requestedOutput + 1] || '')
  : path.join(root, 'docs', 'images');
if (!runtimeRoot.startsWith(path.join(root, 'build') + path.sep)) throw new Error('Unsafe documentation runtime path');

const suffix = process.platform === 'win32' ? '.exe' : '';
async function firstExecutable(name) {
  const candidates = [
    path.join(buildRoot, 'native', name + suffix),
    path.join(buildRoot, 'native', 'Release', name + suffix),
    path.join(buildRoot, 'native', 'tests', name + suffix),
    path.join(buildRoot, 'native', 'tests', 'Release', name + suffix),
  ];
  for (const candidate of candidates) {
    try { await access(candidate); return candidate; } catch { /* keep looking */ }
  }
  throw new Error(`Missing ${name}; build the native targets first`);
}

async function run(command, args, options = {}) {
  return exec(command, args, { cwd: root, timeout: 30000, maxBuffer: 8 * 1024 * 1024, ...options });
}

async function waitForAgent(cli, env, agentProcess, readAgentErrors) {
  let lastError;
  for (let attempt = 0; attempt < 40; attempt += 1) {
    if (agentProcess.exitCode !== null) {
      throw new Error(`Documentation agent exited with code ${agentProcess.exitCode}: ${readAgentErrors().trim() || 'no diagnostic output'}`);
    }
    try { await run(cli, ['--json', 'agent', 'status'], { env }); return; }
    catch (error) { lastError = error; await new Promise((resolve) => setTimeout(resolve, 100)); }
  }
  throw new Error(`Documentation agent did not become ready: ${lastError?.message || 'unknown error'}`);
}

await rm(runtimeRoot, { recursive: true, force: true });
await mkdir(path.join(runtimeRoot, 'r'), { recursive: true });
await chmod(path.join(runtimeRoot, 'r'), 0o700);
await mkdir(outputRoot, { recursive: true });

const fixture = await firstExecutable('cybersnapper-doc-fixture');
const agent = await firstExecutable('cybersnapper-agent');
const gui = await firstExecutable('CyberSnapper');
const cli = await firstExecutable('cybersnapper-cli');
const projectRoot = path.join(runtimeRoot, 'project');
const worker = path.join(root, 'native', 'tests', 'fixtures', 'doc-worker.cjs');
const env = {
  ...process.env,
  XDG_CONFIG_HOME: path.join(runtimeRoot, 'config'),
  XDG_DATA_HOME: path.join(runtimeRoot, 'data'),
  XDG_CACHE_HOME: path.join(runtimeRoot, 'cache'),
  XDG_RUNTIME_DIR: path.join(runtimeRoot, 'r'),
  CYBERSNAPPER_DEFAULT_PROJECT: projectRoot,
  CYBERSNAPPER_AGENT_SERVER: path.join(runtimeRoot, 'r', 'a.sock'),
  CYBERSNAPPER_NO_AUTOSTART: '1',
  CYBERSNAPPER_AGENT: agent,
  CYBERSNAPPER_GUI: gui,
  CYBERSNAPPER_NODE: process.execPath,
  CYBERSNAPPER_WORKER_ENTRY: worker,
  CYBERSNAPPER_BROWSER_CACHE: path.join(runtimeRoot, 'browsers'),
  QT_QPA_PLATFORM: process.env.QT_QPA_PLATFORM || 'offscreen',
  CYBERSNAPPER_UI_THEME: 'dark',
  QT_STYLE_OVERRIDE: 'Fusion',
  QT_SCALE_FACTOR: '1',
  TZ: 'UTC',
};

process.stdout.write('seeding documentation project\n');
await run(fixture, [projectRoot], { env });
const agentProcess = spawn(agent, ['--headless'], { cwd: root, env, stdio: ['ignore', 'pipe', 'pipe'] });
let agentErrors = '';
agentProcess.stderr.on('data', (chunk) => { agentErrors += chunk.toString(); });

try {
  process.stdout.write('starting isolated documentation agent\n');
  await waitForAgent(cli, env, agentProcess, () => agentErrors);
  for (const scene of ['dashboard', 'capture', 'targets', 'review', 'history', 'schedules']) {
    const destination = path.join(outputRoot, `app-${scene}.png`);
    process.stdout.write(`capturing ${scene}\n`);
    await run(gui, [], { env: { ...env, CYBERSNAPPER_UI_SCENE: scene, CYBERSNAPPER_UI_SCREENSHOT: destination } });
    const metadata = await sharp(destination).metadata();
    if (metadata.width !== 1280 || metadata.height !== 800 || metadata.format !== 'png') {
      throw new Error(`Unexpected ${scene} screenshot: ${metadata.width}x${metadata.height} ${metadata.format}`);
    }
    process.stdout.write(`generated ${path.relative(root, destination)}\n`);
  }
  const { renderPresentation } = require(path.join(root, 'worker', 'dist', 'testing.cjs'));
  const portfolioSource = await sharp(path.join(projectRoot, 'captures', 'showcase', 'current.png')).png().toBuffer();
  const portfolio = await renderPresentation(portfolioSource, {
    enabled: true, scene: 'aurora', frame: 'lightBrowser', aspect: '16:9',
    padding: 'balanced', shadow: 'soft', solidColor: '#0B1220',
  }, { mobile: false }, 'viewport');
  const portfolioDestination = path.join(outputRoot, 'portfolio-aurora-browser.png');
  await writeFile(portfolioDestination, portfolio.bytes);
  const portfolioMetadata = await sharp(portfolioDestination).metadata();
  if (portfolioMetadata.format !== 'png' || Math.abs((portfolioMetadata.width / portfolioMetadata.height) - (16 / 9)) > 0.002) {
    throw new Error(`Unexpected portfolio presentation example: ${portfolioMetadata.width}x${portfolioMetadata.height} ${portfolioMetadata.format}`);
  }
  process.stdout.write(`generated ${path.relative(root, portfolioDestination)}\n`);
} finally {
  try { await run(cli, ['--force', 'agent', 'stop'], { env }); } catch { agentProcess.kill('SIGTERM'); }
  if (agentProcess.exitCode === null) agentProcess.kill('SIGTERM');
}

if (agentErrors.trim() && !agentErrors.includes('QStandardPaths')) process.stderr.write(agentErrors);
