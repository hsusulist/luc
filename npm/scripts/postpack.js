'use strict';

const fs = require('fs');
const path = require('path');

for (const name of ['install.ps1', 'LICENCE']) {
  try {
    fs.unlinkSync(path.join(process.cwd(), name));
  } catch (_) {
  }
}
