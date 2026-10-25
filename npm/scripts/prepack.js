'use strict';

const fs = require('fs');
const path = require('path');

const root = path.join(__dirname, '..', '..');
const here = process.cwd();

function copy(name) {
  const source = path.join(root, name);
  const target = path.join(here, name);
  if (!fs.existsSync(source)) {
    throw new Error('missing release file: ' + source);
  }
  fs.copyFileSync(source, target);
}

copy('install.ps1');
copy('LICENCE');
