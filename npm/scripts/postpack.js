'use strict';

const fs = require('fs');
const path = require('path');

for (const name of ['install.ps1', 'LICENCE']) {
  try {
    fs.unlinkSync(path.join(process.cwd(), name));
  } catch (_) {
  }
}

const source = path.join(process.cwd(), 'src');
const marker = path.join(process.cwd(), '.npm-source-bundle-generated');
if (fs.existsSync(marker)) {
  fs.rmSync(source, { recursive: true, force: true });
  fs.unlinkSync(marker);
}

const helper = path.join(process.cwd(), 'scripts', 'build-linux.js');
const helperMarker = path.join(process.cwd(), '.npm-build-linux-generated');
if (fs.existsSync(helperMarker)) {
  fs.unlinkSync(helper);
  fs.unlinkSync(helperMarker);
}
