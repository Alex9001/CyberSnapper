const fs = require('fs');

let sharp = null;
try {
  sharp = require('sharp');
} catch {
  // sharp native module not available (e.g. in pkg binary) — WebP/AVIF conversion disabled
}

const SUPPORTED_FORMATS = ['png', 'webp', 'avif', 'pdf'];

function normalizeFormats(formats) {
  if (!Array.isArray(formats)) return ['png'];
  let filtered = formats.filter(f => SUPPORTED_FORMATS.includes(f));
  // Remove webp/avif if sharp is unavailable
  if (!sharp) {
    filtered = filtered.filter(f => f !== 'webp' && f !== 'avif');
  }
  return filtered.length ? filtered : ['png'];
}

async function writeFormat(page, pngBuffer, format, outputPath, opts) {
  if (format === 'png') {
    fs.writeFileSync(outputPath, pngBuffer);
    return;
  }
  if (format === 'webp') {
    if (!sharp) throw new Error('sharp module not available — cannot convert to WebP');
    await sharp(pngBuffer)
      .webp({ quality: opts.webpQuality })
      .toFile(outputPath);
    return;
  }
  if (format === 'avif') {
    if (!sharp) throw new Error('sharp module not available — cannot convert to AVIF');
    await sharp(pngBuffer)
      .avif({ quality: opts.avifQuality })
      .toFile(outputPath);
    return;
  }
  if (format === 'pdf') {
    await page.pdf({
      path: outputPath,
      format: opts.pdf.format,
      landscape: opts.pdf.landscape,
      margin: opts.pdf.margin,
    });
  }
}

module.exports = { SUPPORTED_FORMATS, normalizeFormats, writeFormat };
