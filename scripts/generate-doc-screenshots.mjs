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
  const sourceRoot = path.join(root, 'docs', 'fixtures');
  const loadSource = async (name, width, height) => {
    const source = path.join(sourceRoot, name);
    const metadata = await sharp(source).metadata();
    if (metadata.width !== width || metadata.height !== height || metadata.format !== 'png') {
      throw new Error(`Unexpected CYBER BRAND source ${name}: ${metadata.width}x${metadata.height} ${metadata.format}`);
    }
    return sharp(source).png().toBuffer();
  };
  const lightDesktopSource = await loadSource('cyberbrand-light-desktop.png', 1440, 900);
  const darkDesktopSource = await loadSource('cyberbrand-dark-desktop.png', 1440, 900);
  const lightTabletSource = await loadSource('cyberbrand-light-tablet.png', 768, 1024);
  const darkTabletSource = await loadSource('cyberbrand-dark-tablet.png', 768, 1024);
  const lightMobileSource = await loadSource('cyberbrand-light-mobile.png', 390, 844);
  const darkMobileSource = await loadSource('cyberbrand-dark-mobile.png', 390, 844);
  const desktopViewport = { mobile: false, width: 1440, height: 900 };
  const tabletViewport = { mobile: true, width: 768, height: 1024 };
  const mobileViewport = { mobile: true, width: 390, height: 844 };
  const portfolio = await renderPresentation(darkDesktopSource, {
    enabled: true, scene: 'aurora', frame: 'darkBrowser', aspect: '16:9',
    padding: 'balanced', shadow: 'soft', solidColor: '#0B1220',
  }, desktopViewport, 'viewport');
  const portfolioDestination = path.join(outputRoot, 'portfolio-aurora-browser.png');
  await writeFile(portfolioDestination, portfolio.bytes);
  const portfolioMetadata = await sharp(portfolioDestination).metadata();
  if (portfolioMetadata.format !== 'png' || Math.abs((portfolioMetadata.width / portfolioMetadata.height) - (16 / 9)) > 0.002) {
    throw new Error(`Unexpected portfolio presentation example: ${portfolioMetadata.width}x${portfolioMetadata.height} ${portfolioMetadata.format}`);
  }
  process.stdout.write(`generated ${path.relative(root, portfolioDestination)}\n`);

  const galleryChoices = [
    { label: 'Clean', detail: 'Clearnet · Light browser', scene: 'clean', frame: 'lightBrowser', source: lightDesktopSource, viewport: desktopViewport },
    { label: 'Aurora', detail: 'Darkweb · Dark browser', scene: 'aurora', frame: 'darkBrowser', source: darkDesktopSource, viewport: desktopViewport },
    { label: 'Sunset', detail: 'Clearnet · Light tablet', scene: 'sunset', frame: 'lightTablet', source: lightTabletSource, viewport: tabletViewport },
    { label: 'Midnight', detail: 'Darkweb · Rounded card', scene: 'midnight', frame: 'roundedCard', source: darkDesktopSource, viewport: desktopViewport },
    { label: 'Graphite', detail: 'Darkweb · Dark phone', scene: 'graphite', frame: 'darkPhone', source: darkMobileSource, viewport: mobileViewport },
    { label: 'Custom solid', detail: 'Clearnet · No frame', scene: 'customSolid', frame: 'none', source: lightDesktopSource, viewport: desktopViewport },
  ];
  const generateGallery = async (choices, title, subtitle, filename, layout = {}) => {
    const {
      galleryWidth = 1800, galleryHeight = 940, columns = 3, marginX = 60,
      columnGap = 30, firstRowY = 140, rowGap = 380, cellWidth = 540,
      imageHeight = 304, cellHeight = 360, captionY = 336,
      labelFontSize = 18, detailFontSize = 14,
    } = layout;
    const background = Buffer.from(`<svg xmlns="http://www.w3.org/2000/svg" width="${galleryWidth}" height="${galleryHeight}">
      <defs><linearGradient id="bg" x1="0" y1="0" x2="1" y2="1"><stop stop-color="#07111F"/><stop offset="1" stop-color="#101D31"/></linearGradient></defs>
      <rect width="100%" height="100%" fill="url(#bg)"/><text x="60" y="62" fill="#EAF7FF" font-family="Arial,sans-serif" font-size="34" font-weight="700">${title}</text>
      <text x="60" y="96" fill="#8FA5B8" font-family="Arial,sans-serif" font-size="18">${subtitle}</text>
      ${choices.map((choice, index) => {
        const x = marginX + (index % columns) * (cellWidth + columnGap);
        const y = firstRowY + Math.floor(index / columns) * rowGap;
        return `<rect x="${x}" y="${y}" width="${cellWidth}" height="${cellHeight}" rx="16" fill="#0C192A" stroke="#243A50"/>
          <text x="${x + 18}" y="${y + captionY}" fill="#EAF7FF" font-family="Arial,sans-serif" font-size="${labelFontSize}" font-weight="700">${choice.label}</text>
          <text x="${x + cellWidth - 18}" y="${y + captionY}" text-anchor="end" fill="#58DDFE" font-family="Arial,sans-serif" font-size="${detailFontSize}">${choice.detail}</text>`;
      }).join('')}
    </svg>`);
    const composites = [];
    for (const [index, choice] of choices.entries()) {
      const rendered = await renderPresentation(choice.source, {
        enabled: true, scene: choice.scene, frame: choice.frame, aspect: '16:9',
        padding: 'balanced', shadow: 'soft', solidColor: '#0F766E',
      }, choice.viewport, 'viewport');
      const thumbnail = await sharp(rendered.bytes).resize(cellWidth, imageHeight, { fit: 'fill' }).png().toBuffer();
      composites.push({ input: thumbnail, left: marginX + (index % columns) * (cellWidth + columnGap),
        top: firstRowY + Math.floor(index / columns) * rowGap });
    }
    const destination = path.join(outputRoot, filename);
    await sharp(background).composite(composites).png({ compressionLevel: 9 }).toFile(destination);
    const metadata = await sharp(destination).metadata();
    if (metadata.width !== galleryWidth || metadata.height !== galleryHeight || metadata.format !== 'png') {
      throw new Error(`Unexpected generated gallery: ${metadata.width}x${metadata.height} ${metadata.format}`);
    }
    process.stdout.write(`generated ${path.relative(root, destination)}\n`);
  };
  await generateGallery(galleryChoices, 'Portfolio presentation, six ways',
    'Real cyberbrand.net Clearnet and Darkweb captures — scenes, browser, tablet, phone, cards, and custom color.',
    'portfolio-scene-gallery.png');
  const frameChoices = [
    { label: 'None', detail: 'Clearnet', scene: 'aurora', frame: 'none', source: lightDesktopSource, viewport: desktopViewport },
    { label: 'Rounded card', detail: 'Darkweb', scene: 'aurora', frame: 'roundedCard', source: darkDesktopSource, viewport: desktopViewport },
    { label: 'Light browser', detail: 'Clearnet', scene: 'aurora', frame: 'lightBrowser', source: lightDesktopSource, viewport: desktopViewport },
    { label: 'Dark browser', detail: 'Darkweb', scene: 'aurora', frame: 'darkBrowser', source: darkDesktopSource, viewport: desktopViewport },
    { label: 'Light tablet', detail: 'Clearnet', scene: 'aurora', frame: 'lightTablet', source: lightTabletSource, viewport: tabletViewport },
    { label: 'Dark tablet', detail: 'Darkweb', scene: 'aurora', frame: 'darkTablet', source: darkTabletSource, viewport: tabletViewport },
    { label: 'Light phone', detail: 'Clearnet', scene: 'aurora', frame: 'lightPhone', source: lightMobileSource, viewport: mobileViewport },
    { label: 'Dark phone', detail: 'Darkweb', scene: 'aurora', frame: 'darkPhone', source: darkMobileSource, viewport: mobileViewport },
  ];
  await generateGallery(frameChoices, 'Frame comparison — every option',
    'Real Clearnet and Darkweb captures on the same Aurora scene isolate desktop, tablet, and phone frames.',
    'portfolio-frame-gallery.png', {
      columns: 4, marginX: 45, columnGap: 25, cellWidth: 405, imageHeight: 228,
      cellHeight: 320, captionY: 276, labelFontSize: 16, detailFontSize: 13,
    });
} finally {
  try { await run(cli, ['--force', 'agent', 'stop'], { env }); } catch { agentProcess.kill('SIGTERM'); }
  if (agentProcess.exitCode === null) agentProcess.kill('SIGTERM');
}

if (agentErrors.trim() && !agentErrors.includes('QStandardPaths')) process.stderr.write(agentErrors);
