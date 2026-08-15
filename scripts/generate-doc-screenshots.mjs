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
  const presentationUiDestination = path.join(outputRoot, 'app-presentation.png');
  process.stdout.write('capturing presentation settings\n');
  await run(gui, [], { env: { ...env, CYBERSNAPPER_UI_SCENE: 'presentation', CYBERSNAPPER_UI_SCREENSHOT: presentationUiDestination } });
  const presentationUiMetadata = await sharp(presentationUiDestination).metadata();
  if (presentationUiMetadata.format !== 'png' || (presentationUiMetadata.width ?? 0) < 700 ||
      (presentationUiMetadata.height ?? 0) < 500) {
    throw new Error(`Unexpected presentation UI screenshot: ${presentationUiMetadata.width}x${presentationUiMetadata.height} ${presentationUiMetadata.format}`);
  }
  process.stdout.write(`generated ${path.relative(root, presentationUiDestination)}\n`);

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

  const mobileSource = await sharp(Buffer.from(`<svg xmlns="http://www.w3.org/2000/svg" width="390" height="844">
    <rect width="390" height="844" fill="#F4F7FB"/><rect width="390" height="76" fill="#07111F"/>
    <text x="24" y="47" fill="#39D7FF" font-family="Arial,sans-serif" font-size="20" font-weight="700">NORTHSTAR</text>
    <circle cx="344" cy="38" r="4" fill="#D6E6F3"/><circle cx="360" cy="38" r="4" fill="#D6E6F3"/>
    <text x="28" y="150" fill="#13253A" font-family="Arial,sans-serif" font-size="38" font-weight="700">Gear for the</text>
    <text x="28" y="196" fill="#13253A" font-family="Arial,sans-serif" font-size="38" font-weight="700">next horizon.</text>
    <text x="28" y="238" fill="#52677C" font-family="Arial,sans-serif" font-size="16">Field-tested essentials for every</text>
    <text x="28" y="262" fill="#52677C" font-family="Arial,sans-serif" font-size="16">viewport and every adventure.</text>
    <rect x="28" y="300" width="334" height="220" rx="28" fill="#DCE8F2"/><rect x="103" y="340" width="184" height="142" rx="20" fill="#0B1D32"/>
    <circle cx="195" cy="411" r="54" fill="#925CFF"/><circle cx="195" cy="411" r="23" fill="#F4F7FB"/>
    <rect x="28" y="560" width="334" height="72" rx="12" fill="#FFFFFF" stroke="#D4E1EC" stroke-width="2"/>
    <text x="48" y="590" fill="#13253A" font-family="Arial,sans-serif" font-size="15" font-weight="700">MOBILE 390</text>
    <text x="48" y="614" fill="#6D8194" font-family="Arial,sans-serif" font-size="12">Navigation wraps cleanly</text>
    <rect x="28" y="664" width="224" height="56" rx="10" fill="#925CFF"/><text x="140" y="699" text-anchor="middle" fill="#FFFFFF" font-family="Arial,sans-serif" font-size="14" font-weight="700">VIEW COLLECTION</text>
  </svg>`)).png().toBuffer();
  const galleryChoices = [
    { label: 'Clean', detail: 'Light browser', scene: 'clean', frame: 'lightBrowser', source: portfolioSource, mobile: false },
    { label: 'Aurora', detail: 'Light browser', scene: 'aurora', frame: 'lightBrowser', source: portfolioSource, mobile: false },
    { label: 'Sunset', detail: 'Rounded card', scene: 'sunset', frame: 'roundedCard', source: portfolioSource, mobile: false },
    { label: 'Midnight', detail: 'Dark browser', scene: 'midnight', frame: 'darkBrowser', source: portfolioSource, mobile: false },
    { label: 'Graphite', detail: 'Dark phone', scene: 'graphite', frame: 'darkPhone', source: mobileSource, mobile: true },
    { label: 'Custom solid', detail: 'No frame', scene: 'customSolid', frame: 'none', source: portfolioSource, mobile: false },
  ];
  const galleryWidth = 1800;
  const galleryHeight = 940;
  const cellWidth = 540;
  const imageHeight = 304;
  const cellHeight = 360;
  const galleryBackground = Buffer.from(`<svg xmlns="http://www.w3.org/2000/svg" width="${galleryWidth}" height="${galleryHeight}">
    <defs><linearGradient id="bg" x1="0" y1="0" x2="1" y2="1"><stop stop-color="#07111F"/><stop offset="1" stop-color="#101D31"/></linearGradient></defs>
    <rect width="100%" height="100%" fill="url(#bg)"/><text x="60" y="62" fill="#EAF7FF" font-family="Arial,sans-serif" font-size="34" font-weight="700">Portfolio presentation, six ways</text>
    <text x="60" y="96" fill="#8FA5B8" font-family="Arial,sans-serif" font-size="18">Generated locally from the same capture — backgrounds, browser chrome, cards, phone hardware, and custom color.</text>
    ${galleryChoices.map((choice, index) => {
      const x = 60 + (index % 3) * 570;
      const y = 140 + Math.floor(index / 3) * 380;
      return `<rect x="${x}" y="${y}" width="${cellWidth}" height="${cellHeight}" rx="16" fill="#0C192A" stroke="#243A50"/>
        <text x="${x + 18}" y="${y + 336}" fill="#EAF7FF" font-family="Arial,sans-serif" font-size="18" font-weight="700">${choice.label}</text>
        <text x="${x + cellWidth - 18}" y="${y + 336}" text-anchor="end" fill="#58DDFE" font-family="Arial,sans-serif" font-size="14">${choice.detail}</text>`;
    }).join('')}
  </svg>`);
  const galleryComposites = [];
  for (const [index, choice] of galleryChoices.entries()) {
    const rendered = await renderPresentation(choice.source, {
      enabled: true, scene: choice.scene, frame: choice.frame, aspect: '16:9',
      padding: 'balanced', shadow: 'soft', solidColor: '#0F766E',
    }, { mobile: choice.mobile }, 'viewport');
    const thumbnail = await sharp(rendered.bytes).resize(cellWidth, imageHeight, { fit: 'fill' }).png().toBuffer();
    galleryComposites.push({ input: thumbnail, left: 60 + (index % 3) * 570,
      top: 140 + Math.floor(index / 3) * 380 });
  }
  const galleryDestination = path.join(outputRoot, 'portfolio-scene-gallery.png');
  await sharp(galleryBackground).composite(galleryComposites).png({ compressionLevel: 9 }).toFile(galleryDestination);
  const galleryMetadata = await sharp(galleryDestination).metadata();
  if (galleryMetadata.width !== galleryWidth || galleryMetadata.height !== galleryHeight || galleryMetadata.format !== 'png') {
    throw new Error(`Unexpected portfolio scene gallery: ${galleryMetadata.width}x${galleryMetadata.height} ${galleryMetadata.format}`);
  }
  process.stdout.write(`generated ${path.relative(root, galleryDestination)}\n`);
} finally {
  try { await run(cli, ['--force', 'agent', 'stop'], { env }); } catch { agentProcess.kill('SIGTERM'); }
  if (agentProcess.exitCode === null) agentProcess.kill('SIGTERM');
}

if (agentErrors.trim() && !agentErrors.includes('QStandardPaths')) process.stderr.write(agentErrors);
