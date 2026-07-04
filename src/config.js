const fs = require('fs');
const path = require('path');

const DEFAULTS = {
  presets: [
    { name: 'Desktop',  width: 1920, height: 1080 },
    { name: 'Tablet',   width: 768,  height: 1024 },
    { name: 'Mobile',   width: 375,  height: 812 },
  ],
};

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
      return { presets: [...DEFAULTS.presets] };
    }
    return parsed;
  } catch {
    return { presets: [...DEFAULTS.presets] };
  }
}

function save(data) {
  fs.writeFileSync(configPath(), JSON.stringify(data, null, 2), 'utf-8');
}

function getPresets() {
  return load().presets;
}

function addPreset(name, width, height) {
  const cfg = load();
  if (cfg.presets.some(p => p.name === name)) return false;
  cfg.presets.push({ name, width, height });
  save(cfg);
  return true;
}

function removePreset(name) {
  const cfg = load();
  const idx = cfg.presets.findIndex(p => p.name === name);
  if (idx === -1) return false;
  cfg.presets.splice(idx, 1);
  save(cfg);
  return true;
}

function resetPresets() {
  save({ presets: [...DEFAULTS.presets] });
}

module.exports = { load, save, getPresets, addPreset, removePreset, resetPresets, configPath };
