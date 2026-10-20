#!/usr/bin/env node
'use strict';

const fs = require('fs');
const crypto = require('crypto');

function fail(msg) {
  console.error('lucsig: ' + msg);
  process.exit(1);
}

function readKey(file) {
  let key;
  try {
    key = JSON.parse(fs.readFileSync(file, 'utf8'));
  } catch (err) {
    fail('cannot read signing key ' + file + ': ' + err.message);
  }
  if (key.type !== 'luc-ed25519' || !key.keyId || !key.privateKey || !key.publicKey) {
    fail('unsupported signing key format in ' + file);
  }
  return key;
}

function keygen(args) {
  const out = valueOf(args, '--out');
  const pubOut = valueOf(args, '--pub');
  if (!out || !pubOut) fail('usage: lucsig.js keygen --out <private-key.json> --pub <public-key.pub>');
  if (fs.existsSync(out)) fail('refusing to overwrite existing key: ' + out);

  const pair = crypto.generateKeyPairSync('ed25519');
  const keyId = crypto.randomBytes(8);
  const rawPublic = pair.publicKey.export({ format: 'der', type: 'spki' }).slice(-32);
  const publicBlob = Buffer.concat([Buffer.from('Ed'), keyId, rawPublic]);
  const privateDer = pair.privateKey.export({ format: 'der', type: 'pkcs8' });

  const key = {
    type: 'luc-ed25519',
    keyId: keyId.toString('hex'),
    publicKey: publicBlob.toString('base64'),
    privateKey: privateDer.toString('base64')
  };

  fs.mkdirSync(require('path').dirname(out), { recursive: true });
  fs.mkdirSync(require('path').dirname(pubOut), { recursive: true });
  fs.writeFileSync(out, JSON.stringify(key, null, 2) + '\n', { mode: 0o600 });
  fs.writeFileSync(pubOut,
    'untrusted comment: minisign public key ' + key.keyId + '\r\n' +
    publicBlob.toString('base64') + '\r\n');

  console.log('private key: ' + out + '  (keep this file secret and back it up)');
  console.log('public key : ' + pubOut);
  console.log('install.ps1 public key value: ' + key.publicKey);
}

function sign(args) {
  const file = args[0];
  const keyFile = valueOf(args, '--key') || process.env.LUC_SIGNING_KEY;
  if (!file || !keyFile) fail('usage: lucsig.js sign <file> --key <private-key.json>');
  const key = readKey(keyFile);
  const data = fs.readFileSync(file);
  const privateKey = crypto.createPrivateKey({
    key: Buffer.from(key.privateKey, 'base64'),
    format: 'der',
    type: 'pkcs8'
  });
  const signature = crypto.sign(null, data, privateKey);
  const blob = Buffer.concat([Buffer.from('Ed'), Buffer.from(key.keyId, 'hex'), signature]);
  const trusted = 'trusted comment: luc release signature';
  const global = crypto.sign(null, Buffer.concat([blob, Buffer.from(trusted)]), privateKey);
  const text =
    'untrusted comment: signature from luc release key\r\n' +
    blob.toString('base64') + '\r\n' +
    trusted + '\r\n' +
    global.toString('base64') + '\r\n';
  fs.writeFileSync(file + '.minisig', text);
  console.log('signed ' + file + ' -> ' + file + '.minisig');
}

function verify(args) {
  const file = args[0];
  const sigFile = args[1] || (file && file + '.minisig');
  const publicBlob = args[2] ? Buffer.from(args[2], 'base64') : null;
  if (!file || !sigFile || !publicBlob) fail('usage: lucsig.js verify <file> [signature] <public-key-base64>');
  const lines = fs.readFileSync(sigFile, 'utf8').split(/\r?\n/).filter(Boolean);
  if (lines.length < 2) fail('invalid signature file');
  const blob = Buffer.from(lines[1], 'base64');
  if (blob.length !== 74) fail('invalid signature length');
  const alg = blob.slice(0, 2).toString('ascii');
  if (alg !== 'Ed') fail('unsupported signature algorithm: ' + alg);
  if (!blob.slice(2, 10).equals(publicBlob.slice(2, 10))) fail('signature key id does not match');
  const rawPublic = publicBlob.slice(10, 42);
  const spki = Buffer.concat([
    Buffer.from('302a300506032b6570032100', 'hex'),
    rawPublic
  ]);
  const keyObject = crypto.createPublicKey({ key: spki, format: 'der', type: 'spki' });
  const ok = crypto.verify(null, fs.readFileSync(file), keyObject, blob.slice(10, 74));
  console.log(ok ? 'signature OK' : 'signature INVALID');
  process.exit(ok ? 0 : 1);
}

function valueOf(args, name) {
  const i = args.indexOf(name);
  return i >= 0 ? args[i + 1] : null;
}

const args = process.argv.slice(2);
const command = args.shift();
if (command === 'keygen') keygen(args);
else if (command === 'sign') sign(args);
else if (command === 'verify') verify(args);
else {
  console.error('usage: lucsig.js <keygen|sign|verify> ...');
  process.exit(1);
}
