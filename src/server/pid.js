const fs = require('fs');
const path = require('path');
const config = require('../config');

const PID_FILENAME = '.cybersnapper.pid';

function pidFilePath() {
  return path.join(path.dirname(config.configPath()), PID_FILENAME);
}

function isProcessAlive(pid) {
  try {
    process.kill(pid, 0);
    return true;
  } catch (err) {
    return err.code === 'EPERM';
  }
}

function readPid() {
  try {
    return parseInt(fs.readFileSync(pidFilePath(), 'utf-8').trim(), 10);
  } catch {
    return NaN;
  }
}

function removePidFile() {
  try { fs.unlinkSync(pidFilePath()); } catch {}
}

function writePidFile() {
  fs.writeFileSync(pidFilePath(), String(process.pid));
  return pidFilePath();
}

/* Atomically claim the PID file. Returns { pidFile, ok }.
   ok=false when another live instance owns it. */
function acquirePidFile() {
  const pidFile = pidFilePath();
  const existing = readPid();
  if (Number.isFinite(existing) && isProcessAlive(existing) && existing !== process.pid) {
    return { pidFile, ok: false, ownerPid: existing };
  }
  if (Number.isFinite(existing)) removePidFile();
  fs.writeFileSync(pidFile, String(process.pid));
  return { pidFile, ok: true };
}

function stopRunningInstance() {
  const pid = readPid();
  if (!Number.isFinite(pid)) {
    console.log('  CyberSnapper is not running.');
    return;
  }
  if (!isProcessAlive(pid)) {
    removePidFile();
    console.log('  CyberSnapper is not running. (cleaned stale PID file)');
    return;
  }
  try {
    process.kill(pid, 'SIGTERM');
  } catch (err) {
    console.error('  Failed to stop process:', err.message);
    process.exit(1);
  }
  console.log(`  Stopping CyberSnapper (PID ${pid})...`);

  const deadline = Date.now() + 3000;
  const check = setInterval(() => {
    if (!isProcessAlive(pid) || Date.now() > deadline) {
      clearInterval(check);
      if (isProcessAlive(pid)) {
        try { process.kill(pid, 'SIGKILL'); } catch {}
        console.log('  Force-killed (SIGKILL).');
      } else {
        console.log('  Stopped.');
      }
      removePidFile();
    }
  }, 100);
}

module.exports = {
  PID_FILENAME,
  pidFilePath,
  isProcessAlive,
  readPid,
  removePidFile,
  writePidFile,
  acquirePidFile,
  stopRunningInstance,
};
