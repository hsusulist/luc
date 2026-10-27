'use strict';

const path = require('path');
const { spawnSync } = require('child_process');

if (process.platform === 'linux') {
  const { buildLinux } = require('./build-linux');
  buildLinux(path.join(__dirname, '..'), 'lcode');
  process.exit(0);
}

if (process.platform !== 'win32') {
  console.error('lcode: this package supports Windows and Linux only.');
  process.exit(1);
}

const script = path.join(__dirname, '..', 'install.ps1');
const result = spawnSync(
  'powershell.exe',
  [
    '-NoProfile',
    '-ExecutionPolicy',
    'Bypass',
    '-File',
    script,
    '-LCodeOnly',
    '-Yes',
    '-Quiet',
    '-NoPath',
    '-NoEnvironmentChanges'
  ],
  { stdio: 'inherit' }
);

if (result.error) {
  console.error('lcode: could not install the standalone editor: ' + result.error.message);
  process.exit(1);
}
if (result.status !== 0) {
  console.error('lcode: standalone install failed; run "lcode --install" to retry.');
  process.exit(result.status === null ? 1 : result.status);
}
