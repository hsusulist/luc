#!/usr/bin/env node
'use strict';

const fs = require('fs');
const os = require('os');
const path = require('path');
const { spawnSync } = require('child_process');

function stateBinary() {
  const local = process.env.LOCALAPPDATA || path.join(os.homedir(), 'AppData', 'Local');
  try {
    const raw = fs.readFileSync(path.join(local, 'LUC', 'state.json'), 'utf8').replace(/^\uFEFF/, '');
    const state = JSON.parse(raw);
    if (state && typeof state.installPath === 'string' && state.installPath) {
      return path.join(state.installPath, 'luc.exe');
    }
  } catch (_) {
  }
  return null;
}

function binaryCandidates() {
  const candidates = [];
  const fromState = stateBinary();
  if (fromState) candidates.push(fromState);
  if (process.env.LUC_HOME) {
    candidates.push(path.join(process.env.LUC_HOME, 'luc.exe'));
  }
  if (process.env.LOCALAPPDATA) {
    candidates.push(path.join(process.env.LOCALAPPDATA, 'Programs', 'LUC', 'luc.exe'));
  }
  if (process.env.ProgramFiles) {
    candidates.push(path.join(process.env.ProgramFiles, 'LUC', 'luc.exe'));
  }
  if (process.env['ProgramFiles(x86)']) {
    candidates.push(path.join(process.env['ProgramFiles(x86)'], 'LUC', 'luc.exe'));
  }
  candidates.push(path.join(os.homedir(), 'AppData', 'Local', 'Programs', 'LUC', 'luc.exe'));
  return candidates;
}

function findBinary() {
  for (const candidate of binaryCandidates()) {
    try {
      if (fs.statSync(candidate).isFile()) return candidate;
    } catch (_) {
    }
  }
  return null;
}

function installerScript() {
  const local = path.join(__dirname, '..', 'install.ps1');
  return fs.existsSync(local) ? local : null;
}

function runInstaller(args) {
  const script = installerScript();
  if (!script) {
    console.error('luc: install.ps1 is missing from the npm package. Reinstall @hsusulist/luc.');
    return 1;
  }
  const result = spawnSync(
    'powershell.exe',
    ['-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', script].concat(args),
    { stdio: 'inherit' }
  );
  if (result.error) {
    console.error('luc: could not start powershell.exe: ' + result.error.message);
    return 1;
  }
  return result.status === null ? 1 : result.status;
}

function mapInstallerArgs(args) {
  const map = {
    '-y': '-Yes',
    '--yes': '-Yes',
    '-f': '-Force',
    '--force': '-Force',
    '-c': '-Components',
    '--components': '-Components',
    '-d': '-InstallPath',
    '--dir': '-InstallPath',
    '--install-path': '-InstallPath',
    '--archive': '-ArchivePath',
    '--source': '-Source',
    '--no-path': '-NoPath',
    '--no-env': '-NoEnvironmentChanges',
    '--quiet': '-Quiet',
    '--no-color': '-NoColor',
    '--allow-unsigned': '-AllowUnsigned'
  };
  return args.map((arg) => (Object.prototype.hasOwnProperty.call(map, arg) ? map[arg] : arg));
}

if (process.platform !== 'win32') {
  console.error('luc: this package currently supports Windows only.');
  process.exit(1);
}

const args = process.argv.slice(2);
let binary = findBinary();

if (!binary) {
  const isInstall = args[0] === '--install';
  const status = runInstaller(isInstall ? mapInstallerArgs(args.slice(1)) : []);
  if (status !== 0) process.exit(status);
  if (isInstall) process.exit(0);
  binary = findBinary();
  if (!binary) {
    console.error('luc: installation finished but luc.exe was not found.');
    process.exit(1);
  }
}

const result = spawnSync(binary, args, { stdio: 'inherit' });
if (result.error) {
  console.error('luc: ' + result.error.message);
  process.exit(1);
}
process.exit(result.status === null ? 1 : result.status);
