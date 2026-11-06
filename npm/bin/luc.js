#!/usr/bin/env node
'use strict';

const fs = require('fs');
const os = require('os');
const path = require('path');
const { spawnSync } = require('child_process');
const http = require('http');
const https = require('https');
const { promisify } = require('util');

const args = process.argv.slice(2);

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

const sleep = promisify(setTimeout);
const registry = (process.env.LUC_REGISTRY_URL || 'http://localhost:3000').replace(/\/+$/, '');

function request(url, options = {}) {
  return new Promise((resolve, reject) => {
    const target = new URL(url);
    const client = target.protocol === 'https:' ? https : http;
    const req = client.request(target, {
      method: options.method || 'GET',
      headers: options.headers || {}
    }, (res) => {
      const chunks = [];
      res.on('data', (chunk) => chunks.push(chunk));
      res.on('end', () => resolve({
        status: res.statusCode || 0,
        headers: res.headers,
        body: Buffer.concat(chunks)
      }));
    });
    req.on('error', reject);
    if (options.body) req.write(options.body);
    req.end();
  });
}

async function requestJson(pathname, options = {}) {
  const headers = Object.assign({}, options.headers || {});
  let body = options.body;
  if (body && !Buffer.isBuffer(body) && typeof body !== 'string') {
    body = JSON.stringify(body);
    headers['Content-Type'] = 'application/json';
  }
  const response = await request(registry + pathname, { method: options.method, headers, body });
  let payload;
  try {
    payload = JSON.parse(response.body.toString('utf8'));
  } catch (_) {
    payload = { error: response.body.toString('utf8') || 'Unexpected server response.' };
  }
  if (response.status < 200 || response.status >= 300) {
    throw new Error(payload.error || 'Request failed with status ' + response.status + '.');
  }
  return payload;
}

function authPath() {
  const base = process.env.LOCALAPPDATA || process.env.XDG_CONFIG_HOME || path.join(os.homedir(), '.config');
  const terminal = process.env.LUC_TERMINAL_SESSION || String(process.ppid);
  const key = require('crypto').createHash('sha256').update(terminal).digest('hex').slice(0, 24);
  const dir = path.join(base, 'LUC', 'auth');
  fs.mkdirSync(dir, { recursive: true });
  return path.join(dir, key + '.json');
}

function readAuth() {
  try {
    return JSON.parse(fs.readFileSync(authPath(), 'utf8'));
  } catch (_) {
    return null;
  }
}

function saveAuth(auth) {
  fs.writeFileSync(authPath(), JSON.stringify(auth), { mode: 0o600 });
}

function clearAuth() {
  try { fs.unlinkSync(authPath()); } catch (error) {
    if (error.code !== 'ENOENT') throw error;
  }
}

function openBrowser(url) {
  let command;
  let args;
  if (process.platform === 'win32') {
    command = 'rundll32.exe';
    args = ['url.dll,FileProtocolHandler', url];
  } else if (process.platform === 'darwin') {
    command = 'open';
    args = [url];
  } else {
    command = 'xdg-open';
    args = [url];
  }
  const child = spawnSync(command, args, { stdio: 'ignore' });
  if (child.error || child.status !== 0) {
    console.error('luc: could not open a browser. Open this URL manually:\n' + url);
  }
}

async function login() {
  const started = await requestJson('/api/auth/cli/start', { method: 'POST', body: {} });
  console.log('Waiting for LUC browser sign-in…');
  openBrowser(started.loginUrl);
  const deadline = Date.now() + 10 * 60 * 1000;
  while (Date.now() < deadline) {
    await sleep(1500);
    const response = await request(registry + '/api/auth/cli/poll', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ code: started.code })
    });
    if (response.status === 202) continue;
    let data;
    try { data = JSON.parse(response.body.toString('utf8')); } catch (_) { data = {}; }
    if (response.status < 200 || response.status >= 300) throw new Error(data.error || 'Sign-in failed.');
    saveAuth({ token: data.token, username: data.user.username });
    console.log('Logged in as @' + data.user.username + ' for this terminal.');
    return;
  }
  throw new Error('Sign-in timed out. Run luc login again.');
}

async function whoami() {
  const auth = readAuth();
  if (!auth || !auth.token) throw new Error('Not logged in. Run "luc login" in this terminal.');
  try {
    const data = await requestJson('/api/auth/me', { headers: { Authorization: 'Bearer ' + auth.token } });
    console.log('@' + data.user.username);
  } catch (error) {
    clearAuth();
    throw error;
  }
}

async function installPackage(identifier) {
  const match = identifier.match(/^@([a-z0-9_-]{3,32})\/([a-z][a-z0-9-]{1,38}[a-z0-9])$/i);
  if (!match) throw new Error('Use a package name like @username/packagename.');
  const username = match[1].toLowerCase();
  const name = match[2].toLowerCase();
  const data = await requestJson('/api/packages?q=' + encodeURIComponent(name) + '&sort=downloads');
  const pkg = (data.packages || []).find((item) =>
    item.name.toLowerCase() === name && String(item.author.handle || '').toLowerCase() === username
  );
  if (!pkg) throw new Error('No package named ' + identifier + ' was found.');
  const response = await request(registry + '/api/packages/' + encodeURIComponent(pkg.slug) + '/file');
  if (response.status < 200 || response.status >= 300) {
    let message = response.body.toString('utf8');
    try { message = JSON.parse(message).error || message; } catch (_) {}
    if (pkg.official && ['libs', 'net', 'json'].includes(pkg.name)) {
      await requestJson('/api/packages/' + encodeURIComponent(pkg.slug) + '/install', { method: 'POST', body: {} });
      console.log(pkg.name + ' is already included with the LUC runtime.');
      return;
    }
    if (pkg.official && ['window', 'ai', 'discord'].includes(pkg.name)) {
      const binary = process.platform === 'linux'
        ? path.join(__dirname, '..', 'native', 'luc')
        : findBinary();
      if (!binary || !fs.existsSync(binary)) throw new Error('The LUC runtime is missing. Run "luc install" first.');
      const result = spawnSync(binary, ['install', pkg.name], { stdio: 'inherit' });
      if (result.error || result.status !== 0) throw new Error(result.error ? result.error.message : 'LUC could not install ' + pkg.name + '.');
      await requestJson('/api/packages/' + encodeURIComponent(pkg.slug) + '/install', { method: 'POST', body: {} });
      return;
    }
    throw new Error(message || 'The package has no downloadable .luic file.');
  }
  const destination = path.resolve(process.cwd(), pkg.name + '.luic');
  if (fs.existsSync(destination)) throw new Error(pkg.name + '.luic already exists in this folder.');
  fs.writeFileSync(destination, response.body, { flag: 'wx' });
  await requestJson('/api/packages/' + encodeURIComponent(pkg.slug) + '/install', { method: 'POST', body: {} });
  console.log('Installed ' + identifier + ' to ' + destination);
}

async function publishPackage() {
  const auth = readAuth();
  if (!auth || !auth.token) throw new Error('Not logged in. Run "luc login" in this terminal first.');
  const files = fs.readdirSync(process.cwd()).filter((name) => name.toLowerCase().endsWith('.luic'));
  if (files.length === 0) throw new Error('No .luic file found in the current folder.');
  if (files.length > 1) throw new Error('Found multiple .luic files. Keep only the package you want to publish in this folder.');
  const filename = files[0];
  const bytes = fs.readFileSync(path.join(process.cwd(), filename));
  const upload = await requestJson('/api/auth/cli/uploads', {
    method: 'POST',
    headers: {
      Authorization: 'Bearer ' + auth.token,
      'Content-Type': 'application/octet-stream',
      'X-File-Name': filename
    },
    body: bytes
  });
  const readmePath = ['README.md', 'readme.md', 'README'].map((name) => path.join(process.cwd(), name)).find(fs.existsSync);
  const readme = readmePath ? fs.readFileSync(readmePath, 'utf8') : '';
  if (readme.length > 60000) throw new Error('README.md must be 60,000 characters or fewer.');
  await requestJson('/api/auth/cli/uploads/' + encodeURIComponent(upload.id) + '/readme', {
    method: 'POST',
    headers: { Authorization: 'Bearer ' + auth.token },
    body: { readme }
  });
  const publishUrl = registry + '/#/publish?upload=' + encodeURIComponent(upload.id);
  console.log('Package file and README attached. Finish the package name and description in your browser.');
  openBrowser(publishUrl);
}

async function handleSiteCommand() {
  const [command, ...rest] = args;
  if (command === 'login') {
    if (rest.length) throw new Error('Usage: luc login');
    return login();
  }
  if (command === 'whoami') {
    if (rest.length) throw new Error('Usage: luc whoami');
    return whoami();
  }
  if (command === 'logout') {
    if (rest.length) throw new Error('Usage: luc logout');
    const auth = readAuth();
    if (auth && auth.token) {
      try {
        await requestJson('/api/auth/logout', {
          method: 'POST',
          headers: { Authorization: 'Bearer ' + auth.token },
          body: {}
        });
      } catch (_) {
      }
    }
    clearAuth();
    console.log('Logged out of this terminal.');
    return;
  }
  if (command === 'publish') {
    if (rest.length !== 1 || rest[0] !== '-p') throw new Error('Usage: luc publish -p');
    return publishPackage();
  }
  if (command === 'install' && rest[0] === '-p') {
    if (rest.length !== 2 || !rest[1]) throw new Error('Usage: luc install -p @username/packagename');
    return installPackage(rest[1]);
  }
  return null;
}

async function runSiteCommand() {
  try {
    const handled = await handleSiteCommand();
    if (handled !== null) return true;
    return false;
  } catch (error) {
    console.error('luc: ' + (error.message || error));
    process.exitCode = 1;
    return true;
  }
}

if (args[0] === 'login' || args[0] === 'whoami' || args[0] === 'logout' || args[0] === 'publish') {
  runSiteCommand().then((handled) => { if (!handled) process.exitCode = 1; });
  return;
}

if (args[0] === 'install' && args[1] === '-p') {
  runSiteCommand().then((handled) => { if (!handled) process.exitCode = 1; });
  return;
}

if (args[0] === 'install' && (args.length === 1 || args[1].startsWith('-'))) {
  if (process.platform === 'linux') {
    try {
      const packageDir = path.join(__dirname, '..');
      const { buildLinux } = require('../scripts/build-linux');
      buildLinux(packageDir, 'luc');
    } catch (error) {
      console.error('luc: ' + error.message);
      process.exitCode = 1;
    }
  } else if (process.platform === 'win32') {
    process.exitCode = runInstaller(mapInstallerArgs(args.slice(1)));
  } else {
    console.error('luc: this package supports Windows and Linux only.');
    process.exitCode = 1;
  }
  return;
}

if (process.platform === 'linux') {
  const packageDir = path.join(__dirname, '..');
  const binary = path.join(packageDir, 'native', 'luc');
  if (!fs.existsSync(binary)) {
    console.error('luc: Linux runtime is missing; run "luc install" to build it.');
    process.exit(1);
  }
  const result = spawnSync(binary, args, { stdio: 'inherit' });
  if (result.error) {
    console.error('luc: ' + result.error.message);
    process.exit(1);
  }
  process.exit(result.status === null ? 1 : result.status);
}

if (process.platform !== 'win32') {
  console.error('luc: this package supports Windows and Linux only.');
  process.exit(1);
}

let binary = findBinary();

if (!binary) {
  const status = runInstaller([]);
  if (status !== 0) process.exit(status);
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
