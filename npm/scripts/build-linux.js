'use strict';

const fs = require('fs');
const path = require('path');
const { spawnSync } = require('child_process');

function buildLinux(packageDir, name) {
  const sourceDir = path.join(packageDir, 'src');
  const files = ['luc_core.c', 'luc_libs.c', 'luc_trans.c'].map((file) =>
    path.join(sourceDir, file)
  );
  for (const file of files) {
    if (!fs.existsSync(file)) {
      throw new Error('Linux source is missing from this npm package: ' + file);
    }
  }

  const outputDir = path.join(packageDir, 'native');
  const output = path.join(outputDir, name);
  fs.mkdirSync(outputDir, { recursive: true });

  const compiler = process.env.CC || 'cc';
  const args = ['-O2', '-std=gnu99', '-o', output].concat(files, ['-lm', '-ldl']);
  const result = spawnSync(compiler, args, { stdio: 'inherit' });
  if (result.error) {
    throw new Error('could not start ' + compiler + ' to build LUC: ' + result.error.message);
  }
  if (result.status !== 0) {
    throw new Error(compiler + ' failed to build LUC (exit code ' + result.status + ').');
  }
  return output;
}

module.exports = { buildLinux };
