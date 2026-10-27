#!/usr/bin/env node
'use strict';

const fs = require('fs');
const os = require('os');
const path = require('path');
const { spawnSync } = require('child_process');

const args = process.argv.slice(2);

if (process.platform === 'linux') {
  const packageDir = path.join(__dirname, '..');
  const { buildLinux } = require('../scripts/build-linux');
  if (args[0] === '--install') {
    buildLinux(packageDir, 'lcode');
    process.exit(0);
  }
  const binary = path.join(packageDir, 'native', 'lcode');
  if (!fs.existsSync(binary)) {
    console.error('lcode: Linux editor is missing; run "lcode --install" to build it.');
    process.exit(1);
  }
  const result = spawnSync(binary, ['--lcode'].concat(args), {
    stdio: 'inherit',
    cwd: process.cwd()
  });
  if (result.error) {
    console.error('lcode: ' + result.error.message);
    process.exit(1);
  }
  process.exit(result.status === null ? 1 : result.status);
}

if (process.platform !== 'win32') {
  console.error('lcode: this package supports Windows and Linux only.');
  process.exit(1);
}

const installScript = path.join(__dirname, '..', 'install.ps1');

function findBinary() {
  const candidates = [];
  if (process.env.LCODE_HOME) candidates.push(path.join(process.env.LCODE_HOME, 'lcode.exe'));
  const local = process.env.LOCALAPPDATA || path.join(os.homedir(), 'AppData', 'Local');
  candidates.push(path.join(local, 'Programs', 'LCode', 'lcode.exe'));
  if (process.env.ProgramFiles) candidates.push(path.join(process.env.ProgramFiles, 'LCode', 'lcode.exe'));
  return candidates.find((candidate) => {
    try {
      return fs.statSync(candidate).isFile();
    } catch (_) {
      return false;
    }
  });
}

function runInstaller(installerArgs) {
  if (!fs.existsSync(installScript)) {
    console.error('lcode: install.ps1 is missing from this npm package; reinstall @hsusulist/lcode.');
    return 1;
  }
  const result = spawnSync(
    'powershell.exe',
    ['-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', installScript, '-LCodeOnly'].concat(installerArgs),
    { stdio: 'inherit', cwd: process.cwd() }
  );
  if (result.error) {
    console.error('lcode: could not start powershell.exe: ' + result.error.message);
    return 1;
  }
  return result.status === null ? 1 : result.status;
}

if (args[0] === '--install') {
  const status = runInstaller(args.slice(1));
  process.exit(status);
}

const binary = findBinary();
if (!binary) {
  console.error('lcode: LCode is not installed. Reinstall with "npm install -g @hsusulist/lcode" or run "lcode --install".');
  process.exit(1);
}

const result = spawnSync(binary, args, { stdio: 'inherit', cwd: process.cwd() });
if (result.error) {
  console.error('lcode: ' + result.error.message);
  process.exit(1);
}
process.exit(result.status === null ? 1 : result.status);
