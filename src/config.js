const fs = require('fs');
const path = require('path');
const crypto = require('crypto');

const DEFAULTS = {
  presets: [
    { name: 'Desktop',  width: 1920, height: 1080 },
    { name: 'Tablet',   width: 768,  height: 1024 },
    { name: 'Mobile',   width: 375,  height: 812 },
  ],
  initialDelay: 1.5,
  scrollDelay: 1.8,
  finalDelay: 1.0,
  concurrency: 1,
  formats: ['png'],
  webp: { quality: 80 },
  avif: { quality: 50 },
  pdf: {
    format: 'A4',
    landscape: false,
    margin: '0'
  },
  hideSelectors: [],
  waitForSelector: '',
  blockPopups: false,
  naming: {
    template: '{hostname}-{preset}',
  },
};

const NAMING_PRESETS = [
  { label: 'Default',  template: '{hostname}-{preset}' },
  { label: 'By size',  template: '{hostname}-{width}x{height}' },
  { label: 'By date',  template: '{date}/{hostname}-{preset}' },
  { label: 'Indexed',  template: '{index}-{hostname}-{preset}' },
  { label: 'By domain', template: '{domain}/{preset}/{hostname}' },
];

const NAMING_VARS = [
  '{hostname}', '{preset}', '{width}', '{height}',
  '{domain}', '{url}', '{date}', '{time}', '{index}',
];

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

function load() {
  try {
    const raw = fs.readFileSync(configPath(), 'utf-8');
    const parsed = JSON.parse(raw);
    if (!Array.isArray(parsed.presets) || parsed.presets.length === 0) {
      parsed.presets = [...DEFAULTS.presets];
    }
    if (parsed.initialDelay == null || typeof parsed.initialDelay !== 'number' || parsed.initialDelay < 0) {
      parsed.initialDelay = DEFAULTS.initialDelay;
    }
    if (parsed.scrollDelay == null || typeof parsed.scrollDelay !== 'number' || parsed.scrollDelay < 0) {
      parsed.scrollDelay = DEFAULTS.scrollDelay;
    }
    if (parsed.finalDelay == null || typeof parsed.finalDelay !== 'number' || parsed.finalDelay < 0) {
      parsed.finalDelay = DEFAULTS.finalDelay;
    }
    if (parsed.concurrency == null || typeof parsed.concurrency !== 'number' || parsed.concurrency < 1) {
      parsed.concurrency = DEFAULTS.concurrency;
    }
    if (parsed.formats == null || !Array.isArray(parsed.formats)) {
      parsed.formats = DEFAULTS.formats;
    }
    if (parsed.webp == null || typeof parsed.webp.quality !== 'number') {
      parsed.webp = DEFAULTS.webp;
    }
    if (parsed.avif == null || typeof parsed.avif.quality !== 'number') {
      parsed.avif = DEFAULTS.avif;
    }
    if (parsed.pdf == null) {
      parsed.pdf = DEFAULTS.pdf;
    }
    if (parsed.hideSelectors == null || !Array.isArray(parsed.hideSelectors)) {
      parsed.hideSelectors = DEFAULTS.hideSelectors;
    }
    if (parsed.waitForSelector == null) {
      parsed.waitForSelector = DEFAULTS.waitForSelector;
    }
    if (parsed.blockPopups == null) {
      parsed.blockPopups = DEFAULTS.blockPopups;
    }
    if (!parsed.apiToken) {
      parsed.apiToken = generateToken();
      save(parsed); // Save the generated token
      console.log(`\n🔑 API Token: ${parsed.apiToken}`);
      console.log(`🔗 API Endpoint: http://localhost:${process.env.PORT || 3000}/api/screenshot?url=...&token=${parsed.apiToken}\n`);
    }
    if (!parsed.naming || !parsed.naming.template) {
      parsed.naming = { template: DEFAULTS.naming.template };
    }
    return parsed;
  } catch {
    const config = {
      presets: [...DEFAULTS.presets], 
      initialDelay: DEFAULTS.initialDelay,
      scrollDelay: DEFAULTS.scrollDelay,
      finalDelay: DEFAULTS.finalDelay,
      concurrency: DEFAULTS.concurrency,
      formats: DEFAULTS.formats,
      webp: DEFAULTS.webp,
      avif: DEFAULTS.avif,
      pdf: DEFAULTS.pdf,
      hideSelectors: DEFAULTS.hideSelectors,
      waitForSelector: DEFAULTS.waitForSelector,
      blockPopups: DEFAULTS.blockPopups,
      apiToken: generateToken(),
      naming: { template: DEFAULTS.naming.template }
    };
    save(config); // Save the new config with token
    console.log(`\n🔑 API Token: ${config.apiToken}`);
    console.log(`🔗 API Endpoint: http://localhost:${process.env.PORT || 3000}/api/screenshot?url=...&token=${config.apiToken}\n`);
    return config;
  }
}

function save(data) {
  if (!Array.isArray(data.presets)) data.presets = [...DEFAULTS.presets];
  if (data.initialDelay == null || typeof data.initialDelay !== 'number' || data.initialDelay < 0) data.initialDelay = DEFAULTS.initialDelay;
  if (data.scrollDelay == null || typeof data.scrollDelay !== 'number' || data.scrollDelay < 0) data.scrollDelay = DEFAULTS.scrollDelay;
  if (data.finalDelay == null || typeof data.finalDelay !== 'number' || data.finalDelay < 0) data.finalDelay = DEFAULTS.finalDelay;
  if (data.blockPopups == null) data.blockPopups = DEFAULTS.blockPopups;
  if (!data.naming || !data.naming.template) data.naming = { template: DEFAULTS.naming.template };
  fs.writeFileSync(configPath(), JSON.stringify(data, null, 2), 'utf-8');
}

function getPresets() {
  return load().presets;
}

function getNaming() {
  return load().naming;
}

function getInitialDelay() {
  return load().initialDelay * 1000; // Convert seconds to ms
}

function getScrollDelay() {
  return load().scrollDelay * 1000; // Convert seconds to ms
}

function getFinalDelay() {
  return load().finalDelay * 1000; // Convert seconds to ms
}

function getConcurrency() {
  return load().concurrency;
}

function getFormats() {
  return load().formats;
}

function getWebpQuality() {
  return load().webp.quality;
}

function getAvifQuality() {
  return load().avif.quality;
}

function getPdfOptions() {
  return load().pdf;
}

function getHideSelectors() {
  return load().hideSelectors;
}

function getWaitForSelector() {
  return load().waitForSelector;
}

function getApiToken() {
  return load().apiToken;
}

function getBlockPopups() {
  return load().blockPopups;
}

module.exports = {
  load, save, getPresets, getNaming, 
  getInitialDelay, getScrollDelay, getFinalDelay, 
  getConcurrency, getFormats, 
  getWebpQuality, getAvifQuality, getPdfOptions, 
  getHideSelectors, getWaitForSelector, 
  getApiToken, getBlockPopups, 
  configPath, NAMING_PRESETS, NAMING_VARS, DEFAULTS
};
