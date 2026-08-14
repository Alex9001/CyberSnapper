import { build } from 'esbuild';

await build({
  entryPoints: ['worker/src/main.ts'],
  outfile: 'worker/dist/main.cjs',
  bundle: true,
  platform: 'node',
  format: 'cjs',
  target: 'node20',
  sourcemap: true,
  external: ['playwright', 'sharp'],
  banner: { js: '#!/usr/bin/env node' },
  logLevel: 'info',
});

await build({
  entryPoints: ['worker/src/testing.ts'],
  outfile: 'worker/dist/testing.cjs',
  bundle: true,
  platform: 'node',
  format: 'cjs',
  target: 'node20',
  external: ['playwright', 'sharp'],
  logLevel: 'silent',
});
