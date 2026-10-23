'use strict';

const fs = require('fs');
const os = require('os');
const path = require('path');
const { spawnSync } = require('child_process');

function findBinary() {
  const candidates = [];
  const local = process.env.LOCALAPPDATA || path.join(os.homedir(), 'AppData', 'Local');
  try {
    const raw = fs.readFileSync(path.join(local, 'LUC', 'state.json'), 'utf8').replace(/^\uFEFF/, '');
    const state = JSON.parse(raw);
    if (state && typeof state.installPath === 'string' && state.installPath) {
      candidates.push(path.join(state.installPath, 'luc.exe'));
    }
  } catch (_) {
  }
  if (process.env.LUC_HOME) candidates.push(path.join(process.env.LUC_HOME, 'luc.exe'));
  if (process.env.LOCALAPPDATA) candidates.push(path.join(process.env.LOCALAPPDATA, 'Programs', 'LUC', 'luc.exe'));
  if (process.env.ProgramFiles) candidates.push(path.join(process.env.ProgramFiles, 'LUC', 'luc.exe'));
  candidates.push(path.join(os.homedir(), 'AppData', 'Local', 'Programs', 'LUC', 'luc.exe'));
  return candidates.find((candidate) => {
    try {
      return fs.statSync(candidate).isFile();
    } catch (_) {
      return false;
    }
  });
}

if (process.platform !== 'win32' || findBinary()) {
  process.exit(0);
}

const script = path.join(__dirname, '..', 'install.ps1');
if (!fs.existsSync(script)) {
  console.warn('luc: install.ps1 is missing; run "luc --install" after installing.');
  process.exit(0);
}

console.log('luc: installing the LUC language runtime...');
const result = spawnSync(
  'powershell.exe',
  ['-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', script, '-Yes', '-NoPath', '-Quiet'],
  { stdio: 'inherit' }
);

if (result.error || result.status !== 0) {
  console.warn('luc: automatic install did not finish. Run "luc --install" to open the installer.');
}
process.exit(0);
