const { execSync } = require('child_process');
const fs = require('fs');
const path = require('path');

const ROOT = __dirname;
const DIST = path.join(ROOT, 'dist');

const TARGETS = {
  linux:         { pkg: 'node22-linux-x64',    file: 'CyberSnapper-linux-x64' },
  win32:         { pkg: 'node22-win-x64',      file: 'CyberSnapper-win32-x64.exe' },
  'macos':       { pkg: 'node22-macos-x64',    file: 'CyberSnapper-macos-x64' },
  'macos-arm64': { pkg: 'node22-macos-arm64',  file: 'CyberSnapper-macos-arm64', fallbackToSource: true },
};

// Map Node.js process.platform values to TARGETS keys
const PLATFORM_MAP = {
  linux:  'linux',
  win32:  'win32',
  darwin: 'macos',
};

fs.mkdirSync(DIST, { recursive: true });

console.log('\n  CyberSnapper — Build\n');

// Determine which targets to build
const targetsToBuild = process.argv[2]
  ? process.argv.slice(2)
  : [PLATFORM_MAP[process.platform] || process.platform];

for (const target of targetsToBuild) {
  const info = TARGETS[target];
  if (!info) {
    console.error(`  Unknown target: ${target} (valid: ${Object.keys(TARGETS).join(', ')})`);
    continue;
  }

  // All targets use their explicit filename for consistency
  const binaryPath = path.join(DIST, info.file);

  const extraArgs = info.fallbackToSource ? ' --fallback-to-source' : '';
  console.log(`[1/2] Compiling for ${target} (${info.pkg})...`);
  execSync(
    `npx @yao-pkg/pkg src/index.js --targets ${info.pkg} --output "${binaryPath}" --config package.json${extraArgs}`,
    { stdio: 'inherit', cwd: ROOT }
  );

  const size = (fs.statSync(binaryPath).size / 1024 / 1024).toFixed(1);
  console.log(`  ✓ ${info.file} (${size} MB)`);
}

console.log(`\n✓ Build complete. Output in ${DIST}/`);
console.log(`  NOTE: users need node_modules/ alongside the binary.\n`);
