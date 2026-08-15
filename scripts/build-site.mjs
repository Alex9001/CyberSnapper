import { access, cp, mkdir, readFile, readdir, rm, writeFile } from 'node:fs/promises';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import sharp from 'sharp';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const source = path.join(root, 'site');
const output = path.join(root, 'build', 'pages');
const screenshotSource = path.join(root, 'docs', 'images');
const outputAssets = path.join(output, 'assets');
const outputScreenshots = path.join(outputAssets, 'screenshots');
if (!output.startsWith(path.join(root, 'build') + path.sep)) throw new Error('Unsafe Pages output path');

await rm(output, { recursive: true, force: true });
await mkdir(outputScreenshots, { recursive: true });
await cp(source, output, { recursive: true });

const logo = path.join(root, 'assets', 'logo.png');
await sharp(logo).resize(384, 384, { fit: 'cover' }).png({ compressionLevel: 9 }).toFile(path.join(outputAssets, 'logo.png'));
await sharp(logo).resize(192, 192, { fit: 'cover' }).png({ compressionLevel: 9 }).toFile(path.join(outputAssets, 'logo-192.png'));
await sharp(logo).resize(96, 96, { fit: 'cover' }).png({ compressionLevel: 9 }).toFile(path.join(outputAssets, 'logo-96.png'));
await sharp(logo).resize(48, 48, { fit: 'cover' }).png({ compressionLevel: 9 }).toFile(path.join(outputAssets, 'favicon.png'));

const screenshots = ['capture', 'targets', 'history', 'review'];
for (const name of screenshots) {
  const input = path.join(screenshotSource, `app-${name}.png`);
  const metadata = await sharp(input).metadata();
  if (metadata.width !== 1280 || metadata.height !== 800) {
    throw new Error(`Documentation screenshot app-${name}.png must be 1280x800`);
  }
  await cp(input, path.join(outputScreenshots, `app-${name}.png`));
  await sharp(input).webp({ quality: 84, effort: 5 }).toFile(path.join(outputScreenshots, `app-${name}.webp`));
}
const portfolioExample = path.join(screenshotSource, 'portfolio-aurora-browser.png');
const portfolioMetadata = await sharp(portfolioExample).metadata();
if (!portfolioMetadata.width || !portfolioMetadata.height ||
    Math.abs(portfolioMetadata.width / portfolioMetadata.height - 16 / 9) > 0.002) {
  throw new Error('Portfolio presentation example must be a 16:9 image');
}
await cp(portfolioExample, path.join(outputScreenshots, 'portfolio-aurora-browser.png'));
await sharp(portfolioExample).webp({ quality: 86, effort: 5 })
  .toFile(path.join(outputScreenshots, 'portfolio-aurora-browser.webp'));

const socialText = Buffer.from(`
  <svg width="1280" height="640" xmlns="http://www.w3.org/2000/svg">
    <defs>
      <linearGradient id="bg" x1="0" x2="1" y1="0" y2="1"><stop stop-color="#050b14"/><stop offset="1" stop-color="#0c1c31"/></linearGradient>
      <radialGradient id="glow"><stop stop-color="#1bc5ed" stop-opacity=".3"/><stop offset="1" stop-color="#1bc5ed" stop-opacity="0"/></radialGradient>
      <pattern id="grid" width="38" height="38" patternUnits="userSpaceOnUse"><path d="M38 0H0V38" fill="none" stroke="#6bdcff" stroke-opacity=".06"/></pattern>
    </defs>
    <rect width="1280" height="640" fill="url(#bg)"/><circle cx="1020" cy="90" r="440" fill="url(#glow)"/><rect width="1280" height="640" fill="url(#grid)"/>
    <text x="420" y="226" fill="#eaf7ff" font-family="Arial,Helvetica,sans-serif" font-size="80" font-weight="700" letter-spacing="-4">Cyber<tspan fill="#56ddff">Snapper</tspan></text>
    <text x="423" y="303" fill="#eaf7ff" font-family="Arial,Helvetica,sans-serif" font-size="38" font-weight="600">Capture your work.</text>
    <text x="423" y="353" fill="#56ddff" font-family="Arial,Helvetica,sans-serif" font-size="38" font-weight="600">Build your portfolio.</text>
    <text x="425" y="420" fill="#91a8ba" font-family="Arial,Helvetica,sans-serif" font-size="21">Portfolio-ready screenshots · macOS · Windows · Linux</text>
    <rect x="424" y="468" rx="21" width="184" height="43" fill="#1bc5ed"/><text x="516" y="497" text-anchor="middle" fill="#03131c" font-family="Arial,Helvetica,sans-serif" font-size="17" font-weight="700">OPEN SOURCE</text>
  </svg>`);
const socialLogo = await sharp(logo).resize(300, 300).png().toBuffer();
await sharp({ create: { width: 1280, height: 640, channels: 4, background: '#050b14' } })
  .composite([{ input: socialText }, { input: socialLogo, left: 72, top: 170 }])
  .png({ compressionLevel: 9 })
  .toFile(path.join(outputAssets, 'social-preview.png'));

await writeFile(path.join(output, '.nojekyll'), '');
await writeFile(path.join(output, 'robots.txt'), 'User-agent: *\nAllow: /\n');

const htmlFiles = (await readdir(output)).filter((name) => name.endsWith('.html'));
const missing = [];
for (const name of htmlFiles) {
  const html = await readFile(path.join(output, name), 'utf8');
  const references = [...html.matchAll(/(?:src|href)="([^"#]+)"/g)].map((match) => match[1]);
  for (const reference of references) {
    if (/^(?:https?:|mailto:)/.test(reference)) continue;
    const localPath = path.resolve(output, reference.replace(/^\.\//, ''));
    if (!localPath.startsWith(output + path.sep)) {
      missing.push(`${name}: unsafe path ${reference}`);
      continue;
    }
    try { await access(localPath); } catch { missing.push(`${name}: missing ${reference}`); }
  }
}
if (missing.length) throw new Error(`Pages validation failed:\n${missing.join('\n')}`);

process.stdout.write(`Built validated GitHub Pages site at ${path.relative(root, output)}\n`);
