const path = require('path');

function generateFilename(template, url, preset, index) {
  const urlObj = new URL(url);
  const now = new Date();

  const vars = {
    '{url}': url,
    '{domain}': urlObj.hostname,
    '{hostname}': urlObj.hostname.replace(/[^a-z0-9]/gi, '_').toLowerCase(),
    '{preset}': (preset.name || '').replace(/[^a-z0-9]/gi, '_').toLowerCase(),
    '{width}': String(preset.width),
    '{height}': String(preset.height),
    '{date}': now.toISOString().slice(0, 10),
    '{time}': now.toISOString().slice(11, 19).replace(/:/g, '-'),
    '{index}': String(index + 1).padStart(2, '0'),
  };

  let result = template;
  for (const [k, v] of Object.entries(vars)) {
    result = result.split(k).join(v);
  }

  result = result.replace(/\{[^}]+\}/g, '');

  const parts = result.split('/');
  const filename = parts.pop().replace(/[\/?<>\\:*|"]/g, '_') + '.png';
  const subdir = parts.filter(p => p.length > 0).join('/');

  return { filename, subdir };
}

function previewTemplate(template, sampleUrl, samplePreset) {
  return generateFilename(template, sampleUrl, samplePreset, 0);
}

module.exports = { generateFilename, previewTemplate };
