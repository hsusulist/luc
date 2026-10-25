#!/usr/bin/env node
'use strict';

const { spawnSync } = require('child_process');
const path = require('path');

if (process.platform !== 'win32') {
  console.error('lcode: this package currently supports Windows only.');
  process.exit(1);
}

let launcher;
try {
  launcher = require.resolve('@hsusulist/luc/bin/luc.js');
} catch (error) {
  console.error('lcode: the @hsusulist/luc launcher is unavailable: ' + error.message);
  process.exit(1);
}

const result = spawnSync(process.execPath, [launcher, '--lcode'].concat(process.argv.slice(2)), {
  stdio: 'inherit',
  cwd: process.cwd()
});

if (result.error) {
  console.error('lcode: ' + result.error.message);
  process.exit(1);
}
process.exit(result.status === null ? 1 : result.status);
