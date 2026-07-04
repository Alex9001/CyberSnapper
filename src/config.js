const fs = require('fs');
const path = require('path');

const DEFAULTS = {
  presets: [
    { name: 'Desktop',  width: 1920, height: 1080 },
    { name: 'Tablet',   width: 768,  height: 1024 },
    { name: 'Mobile',   width: 375,  height: 812 },
  ],
  initialDelay: 2000,
  scrollDelay: 1000,
  finalDelay: 1000,
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
    if (parsed.blockPopups == null) {
      parsed.blockPopups = DEFAULTS.blockPopups;
    }
    if (!parsed.naming || !parsed.naming.template) {
      parsed.naming = { template: DEFAULTS.naming.template };
    }
    return parsed;
  } catch {
    return {
      presets: [...DEFAULTS.presets], 
      initialDelay: DEFAULTS.initialDelay,
      scrollDelay: DEFAULTS.scrollDelay,
      finalDelay: DEFAULTS.finalDelay,
      blockPopups: DEFAULTS.blockPopups,
      naming: { template: DEFAULTS.naming.template }
    };
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
  return load().initialDelay;
}

function getScrollDelay() {
  return load().scrollDelay;
}

function getFinalDelay() {
  return load().finalDelay;
}

function getBlockPopups() {
  return load().blockPopups;
}

module.exports = {
  load, save, getPresets, getNaming, 
  getInitialDelay, getScrollDelay, getFinalDelay, getBlockPopups, 
  configPath, NAMING_PRESETS, NAMING_VARS, DEFAULTS
};
