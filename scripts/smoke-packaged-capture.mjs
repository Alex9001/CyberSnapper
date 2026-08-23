#!/usr/bin/env node

import { execFile } from 'node:child_process';
import { mkdir, open, readdir, writeFile } from 'node:fs/promises';
import http from 'node:http';
import path from 'node:path';
import process from 'node:process';
import { promisify } from 'node:util';

const runFile = promisify(execFile);

if (process.argv.length !== 8) {
  console.error('usage: smoke-packaged-capture.mjs <cli> <agent> <worker> <node-runtime> <browser-cache> <state-root>');
  process.exit(2);
}

const [cli, agent, worker, nodeRuntime, browserCache, stateRoot] = process.argv.slice(2).map((argument) => path.resolve(argument));
const runRoot = path.join(stateRoot, `run-${process.pid}-${Date.now()}`);
const projectRoot = path.join(runRoot, 'project');
const projectState = path.join(projectRoot, '.cybersnapper');
// Keep the IPC directory inside the requested work tree while avoiding the
// deeper smoke-state/project nesting that can exceed Unix socket path limits.
const runtimeRoot = path.join(path.resolve(stateRoot, '..', '..'), `cs-smoke-${process.pid}`);

await mkdir(projectState, { recursive: true });
await mkdir(runtimeRoot, { recursive: true, mode: 0o700 });
await open(path.join(projectState, 'project.sqlite'), 'w').then((file) => file.close());
await writeFile(path.join(projectRoot, 'project.cybersnapper.json'), JSON.stringify({
  schemaVersion: 1,
  projectId: 'package-smoke',
  name: 'Packaged capture smoke test',
  createdAt: new Date().toISOString(),
  database: '.cybersnapper/project.sqlite',
  captureRoot: 'captures',
  allowLocalhost: true,
}, null, 2));

const server = http.createServer((request, response) => {
  response.writeHead(200, { 'content-type': 'text/html; charset=utf-8' });
  response.end(`<!doctype html><html><head><style>
    body { margin: 0; min-height: 100vh; display: grid; place-items: center;
      color: #e9f8ff; background: linear-gradient(135deg, #07111f, #123b59); font: 20px system-ui;
      overflow: hidden; }
    main { padding: 48px; border: 1px solid #2bc6ee; border-radius: 24px; }
    #cookieConsent { position: fixed; bottom: 0; left: 0; right: 0; padding: 18px 24px;
      background: #101d2c; border-top: 1px solid #2bc6ee; font-size: 15px;
      display: flex; gap: 16px; align-items: center; }
  </style></head><body>
    <main><h1>CyberSnapper package smoke</h1><p>${request.url}</p></main>
    <div id="cookieConsent" role="dialog" aria-label="Cookie consent">
      <span>We use cookies</span>
      <button id="reject-all" onclick="this.closest('#cookieConsent').remove()">Reject all</button>
      <button onclick="this.closest('#cookieConsent').remove()">Accept all</button>
    </div>
  </body></html>`);
});
await new Promise((resolve, reject) => {
  server.once('error', reject);
  server.listen(0, '127.0.0.1', resolve);
});
const address = server.address();
if (!address || typeof address === 'string') throw new Error('Could not start the package smoke server');

const childEnvironment = {
  ...process.env,
  CYBERSNAPPER_AGENT: agent,
  CYBERSNAPPER_AGENT_SERVER: process.platform === 'win32'
    ? `CyberSnapper.PackageSmoke.${process.pid}`
    : path.join(runtimeRoot, 'agent.sock'),
  CYBERSNAPPER_DEFAULT_PROJECT: projectRoot,
  CYBERSNAPPER_WORKER_ENTRY: worker,
  CYBERSNAPPER_NODE: nodeRuntime,
  CYBERSNAPPER_BROWSER_CACHE: browserCache,
};
if (process.platform === 'win32') {
  childEnvironment.APPDATA = path.join(runRoot, 'appdata');
  childEnvironment.LOCALAPPDATA = path.join(runRoot, 'localappdata');
} else {
  childEnvironment.XDG_CONFIG_HOME = path.join(runRoot, 'config');
  childEnvironment.XDG_DATA_HOME = path.join(runRoot, 'data');
  childEnvironment.XDG_CACHE_HOME = path.join(runRoot, 'cache');
  childEnvironment.XDG_RUNTIME_DIR = runtimeRoot;
}

async function packagedCli(arguments_, timeout = 180_000) {
  const result = await runFile(cli, arguments_, {
    cwd: runRoot,
    env: childEnvironment,
    timeout,
    windowsHide: true,
    maxBuffer: 16 * 1024 * 1024,
  });
  return result.stdout;
}

async function collectPngs(directory) {
  const found = [];
  for (const entry of await readdir(directory, { withFileTypes: true })) {
    const candidate = path.join(directory, entry.name);
    if (entry.isDirectory()) found.push(...await collectPngs(candidate));
    else if (entry.isFile() && entry.name.toLowerCase().endsWith('.png')) found.push(candidate);
  }
  return found;
}

try {
  await packagedCli(['--json', 'projects', 'open', projectRoot]);
  const output = await packagedCli([
    '--json', '--project', 'package-smoke', '--engine', 'chromium',
    '--format', 'png', '--mode', 'viewport', 'capture',
    `http://127.0.0.1:${address.port}/portfolio`,
  ]);
  const result = JSON.parse(output);
  const status = result.job?.status ?? result.status;
  if (status !== 'succeeded') throw new Error(`Packaged capture ended with status ${status ?? 'unknown'}`);
  const jobId = result.job?.id ?? result.jobId;
  if (!jobId) throw new Error('Packaged capture did not report a job id');

  // The default profile ships with cookie-banner removal enabled. With no
  // cached community lists the capture must still succeed via built-in
  // consent handling, and the artifacts must record that provenance.
  const detail = JSON.parse(await packagedCli(['--json', 'job', 'show', jobId]));
  const artifacts = (detail.artifacts ?? detail.job?.artifacts ?? []).filter((artifact) => artifact.format === 'png');
  if (artifacts.length < 3) throw new Error(`Expected three responsive PNG artifacts, found ${artifacts.length}`);
  for (const artifact of artifacts) {
    const blocking = artifact.contentBlocking;
    if (!blocking) throw new Error('Artifact is missing content-blocking provenance');
    if (blocking.consentActionsSucceeded < 1) {
      throw new Error(`Artifact did not record a successful consent action: ${JSON.stringify(blocking)}`);
    }
    if (!blocking.warnings.some((warning) => /snapshot|not.*download/i.test(warning))) {
      throw new Error(`Expected a missing-snapshot warning, found: ${JSON.stringify(blocking.warnings)}`);
    }
  }

  const captures = await collectPngs(path.join(projectRoot, 'captures'));
  if (captures.length < 3) throw new Error(`Expected three responsive PNG captures, found ${captures.length}`);
  console.log(`Packaged Chromium capture succeeded with ${captures.length} PNG files; ` +
              'cookie banner handled and provenance recorded.');
} finally {
  await packagedCli(['--force', 'agent', 'stop'], 30_000).catch(() => {});
  await new Promise((resolve) => server.close(resolve));
}
