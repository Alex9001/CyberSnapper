const fs = require('fs');
const path = require('path');
const crypto = require('crypto');

const DEFAULTS = {
  presets: [
    { name: 'Desktop', width: 1920, height: 1080 },
    { name: 'Tablet',  width: 768,  height: 1024 },
    { name: 'Mobile',  width: 375,  height: 812 },
  ],
  initialDelay: 1.5,
  scrollDelay: 1.8,
  finalDelay: 1.0,
  concurrency: 1,
  formats: ['png'],
  webp: { quality: 80 },
  avif: { quality: 50 },
  pdf: { format: 'A4', landscape: false, margin: '0' },
  hideSelectors: [],
  waitForSelector: '',
  blockPopups: false,
  naming: { template: '{hostname}-{preset}' },
  theme: 'dark',
};

const NAMING_PRESETS = [
  { label: 'Default',    template: '{hostname}-{preset}' },
  { label: 'By size',    template: '{hostname}-{width}x{height}' },
  { label: 'By date',    template: '{date}/{hostname}-{preset}' },
  { label: 'Indexed',    template: '{index}-{hostname}-{preset}' },
  { label: 'By domain',  template: '{domain}/{preset}/{hostname}' },
];

const NAMING_VARS = [
  '{hostname}', '{preset}', '{width}', '{height}',
  '{domain}', '{url}', '{date}', '{time}', '{index}',
];

function isNum(n) {
  return typeof n === 'number' && !Number.isNaN(n);
}

function clampNum(n, fallback) {
  return isNum(n) ? n : fallback;
}

function normalize(data) {
  if (!data || typeof data !== 'object') return { ...DEFAULTS };

  const out = { ...data };

  if (!Array.isArray(out.presets) || out.presets.length === 0) {
    out.presets = [...DEFAULTS.presets];
  } else {
    out.presets = out.presets
      .filter(p => p && typeof p === 'object' && p.name && p.width && p.height)
      .map(p => ({
        name: String(p.name),
        width: Number(p.width) || 0,
        height: Number(p.height) || 0,
      }));
    if (out.presets.length === 0) out.presets = [...DEFAULTS.presets];
  }

  out.initialDelay = clampNum(out.initialDelay, DEFAULTS.initialDelay);
  out.scrollDelay = clampNum(out.scrollDelay, DEFAULTS.scrollDelay);
  out.finalDelay = clampNum(out.finalDelay, DEFAULTS.finalDelay);

  if (!isNum(out.concurrency) || out.concurrency < 1) {
    out.concurrency = DEFAULTS.concurrency;
  }

  if (!Array.isArray(out.formats) || out.formats.length === 0) {
    out.formats = [...DEFAULTS.formats];
  }

  if (!out.webp || !isNum(out.webp.quality)) out.webp = { ...DEFAULTS.webp };
  if (!out.avif || !isNum(out.avif.quality)) out.avif = { ...DEFAULTS.avif };
  if (!out.pdf) out.pdf = { ...DEFAULTS.pdf };

  if (!Array.isArray(out.hideSelectors)) out.hideSelectors = [...DEFAULTS.hideSelectors];
  if (out.waitForSelector == null) out.waitForSelector = DEFAULTS.waitForSelector;
  if (out.blockPopups == null) out.blockPopups = DEFAULTS.blockPopups;

  if (!out.naming || !out.naming.template) {
    out.naming = { template: DEFAULTS.naming.template };
  }

  if (out.theme !== 'light' && out.theme !== 'dark') out.theme = DEFAULTS.theme;

  return out;
}

function configDir() {
  if (process.pkg || fs.existsSync(path.join(path.dirname(process.execPath), 'package.json'))) {
    return path.dirname(process.execPath);
  }
  return path.resolve(__dirname, '..');
}

function configPath() {
  return path.join(configDir(), 'config.json');
}

function generateToken() {
  return crypto.randomBytes(16).toString('hex');
}

function announceToken(token) {
  const port = process.env.PORT || 3000;
  console.log(`\n🔑 API Token: ${token}`);
  console.log(`🔗 API Endpoint: http://localhost:${port}/api/screenshot?url=...&token=${token}\n`);
}

function ensureApiToken(cfg, { announce = false } = {}) {
  if (cfg.apiToken) return cfg;
  cfg.apiToken = generateToken();
  if (announce) announceToken(cfg.apiToken);
  return cfg;
}

function load() {
  let raw;
  try {
    raw = fs.readFileSync(configPath(), 'utf-8');
  } catch {
    const cfg = ensureApiToken(normalize({ ...DEFAULTS }), { announce: true });
    save(cfg, { skipNormalize: true });
    return cfg;
  }

  let parsed;
  try {
    parsed = JSON.parse(raw);
  } catch {
    parsed = { ...DEFAULTS };
  }

  const cfg = normalize(parsed);
  if (!parsed || !parsed.apiToken) {
    ensureApiToken(cfg, { announce: true });
    save(cfg, { skipNormalize: true });
  }
  return cfg;
}

function save(data, { skipNormalize = false } = {}) {
  let prev = {};
  try {
    prev = JSON.parse(fs.readFileSync(configPath(), 'utf-8')) || {};
  } catch {}

  const toSave = skipNormalize ? data : normalize(data);

  if (!toSave.apiToken && prev.apiToken) toSave.apiToken = prev.apiToken;
  if (!toSave.apiToken) toSave.apiToken = generateToken();

  fs.writeFileSync(configPath(), JSON.stringify(toSave, null, 2), 'utf-8');
  return toSave;
}

module.exports = {
  load,
  save,
  configPath,
  DEFAULTS,
  NAMING_PRESETS,
  NAMING_VARS,
};
