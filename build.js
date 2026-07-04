const { execSync } = require('child_process');
const fs = require('fs');
const path = require('path');

const ROOT = __dirname;
const DIST = path.join(ROOT, 'dist');
const BINARY_NAME = process.platform === 'win32' ? 'CyberSnapper.exe' : 'CyberSnapper';
const BINARY_PATH = path.join(DIST, BINARY_NAME);

fs.mkdirSync(DIST, { recursive: true });

console.log('\n  CyberSnapper — Build\n');

console.log('[1/2] Compiling standalone binary...');
execSync(
  `npx @yao-pkg/pkg src/index.js --targets node22-${process.platform}-x64 --output "${BINARY_PATH}" --config package.json`,
  { stdio: 'inherit', cwd: ROOT }
);

const size = (fs.statSync(BINARY_PATH).size / 1024 / 1024).toFixed(1);
console.log(`\n✓ Build complete: ${BINARY_PATH} (${size} MB)\n`);
console.log(`  NOTE: users need node_modules/ alongside the binary.\n`);
