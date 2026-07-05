const fs = require('fs');
const sharp = require('sharp');

const SUPPORTED_FORMATS = ['png', 'webp', 'avif', 'pdf'];

function normalizeFormats(formats) {
  if (!Array.isArray(formats)) return ['png'];
  const filtered = formats.filter(f => SUPPORTED_FORMATS.includes(f));
  return filtered.length ? filtered : ['png'];
}

async function writeFormat(page, pngBuffer, format, outputPath, opts) {
  if (format === 'png') {
    fs.writeFileSync(outputPath, pngBuffer);
    return;
  }
  if (format === 'webp') {
    await sharp(pngBuffer)
      .webp({ quality: opts.webpQuality })
      .toFile(outputPath);
    return;
  }
  if (format === 'avif') {
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
