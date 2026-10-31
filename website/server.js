'use strict';

/*
 * LUC website + package registry.
 *
 * Zero dependencies. Serves the static site from ./public and a small JSON API
 * backed by ./data/packages.json (seeded from ./data/seed.js on first run).
 *
 *   node website/server.js
 *   PORT=3000 node website/server.js
 */

const http = require('http');
const fs = require('fs');
const fsp = require('fs/promises');
const path = require('path');
const crypto = require('crypto');

const ROOT = __dirname;
const PUBLIC_DIR = path.join(ROOT, 'public');
const DATA_DIR = path.join(ROOT, 'data');
const DB_FILE = path.join(DATA_DIR, 'packages.json');
const PORT = Number(process.env.PORT) || 3000;
const HOST = process.env.HOST || '0.0.0.0';

const MAX_BODY = 1024 * 1024;
const ACCENTS = ['#ff7a18', '#8b5cf6', '#38d996', '#5aa9ff', '#f472b4', '#ffd166', '#7dd3fc', '#a3e635', '#fb7185'];
const LICENSES = ['Apache-2.0', 'MIT', 'BSD-2-Clause', 'BSD-3-Clause', 'GPL-3.0', 'LGPL-3.0', 'MPL-2.0', 'Unlicense', 'CC0-1.0', 'Other'];

const MIME = {
  '.html': 'text/html; charset=utf-8',
  '.css': 'text/css; charset=utf-8',
  '.js': 'text/javascript; charset=utf-8',
  '.json': 'application/json; charset=utf-8',
  '.svg': 'image/svg+xml',
  '.png': 'image/png',
  '.jpg': 'image/jpeg',
  '.jpeg': 'image/jpeg',
  '.webp': 'image/webp',
  '.ico': 'image/x-icon',
  '.woff2': 'font/woff2',
  '.woff': 'font/woff',
  '.ttf': 'font/ttf',
  '.txt': 'text/plain; charset=utf-8',
  '.map': 'application/json; charset=utf-8'
};

/* ------------------------------------------------------------------ */
/* database                                                            */
/* ------------------------------------------------------------------ */

let db = null;
let writeQueue = Promise.resolve();

function normalize(pkg) {
  return {
    slug: pkg.slug,
    name: pkg.name,
    importName: pkg.importName || String(pkg.name || pkg.slug || '').replace(/-/g, '_'),
    summary: pkg.summary || '',
    version: pkg.version || '0.1.0',
    license: pkg.license || 'Apache-2.0',
    repo: pkg.repo || '',
    homepage: pkg.homepage || '',
    author: pkg.author || { name: 'anonymous', handle: 'anonymous', url: '', verified: false },
    official: Boolean(pkg.official),
    featured: Boolean(pkg.featured),
    tags: Array.isArray(pkg.tags) ? pkg.tags.slice(0, 8) : [],
    accent: pkg.accent || '#ff7a18',
    size: pkg.size || '—',
    readme: typeof pkg.readme === 'string' ? pkg.readme : '',
    weeklyDownloads: Number(pkg.weeklyDownloads) || 0,
    totalDownloads: Number(pkg.totalDownloads) || 0,
    downloadsHistory: Array.isArray(pkg.downloadsHistory) && pkg.downloadsHistory.length === 7
      ? pkg.downloadsHistory.map((n) => Number(n) || 0)
      : [0, 0, 0, 0, 0, 0, 0],
    publishedAt: pkg.publishedAt || new Date().toISOString().slice(0, 10),
    updatedAt: pkg.updatedAt || new Date().toISOString().slice(0, 10),
    dependencies: pkg.dependencies && typeof pkg.dependencies === 'object' ? pkg.dependencies : {},
    versions: Array.isArray(pkg.versions) ? pkg.versions : [],
    ownerKey: pkg.ownerKey || ''
  };
}

async function loadDb() {
  await fsp.mkdir(DATA_DIR, { recursive: true });
  try {
    const raw = await fsp.readFile(DB_FILE, 'utf8');
    const parsed = JSON.parse(raw);
    db = Array.isArray(parsed) ? parsed.map(normalize) : [];
  } catch (err) {
    if (err.code !== 'ENOENT') {
      console.warn('[luc] could not read packages.json, reseeding:', err.message);
    }
    const seed = require('./data/seed.js');
    db = seed.packages.map((p) => normalize({ ...p, ownerKey: 'official' }));
    await persist();
  }
  console.log(`[luc] registry ready with ${db.length} packages`);
}

function persist() {
  const snapshot = JSON.stringify(db, null, 2);
  writeQueue = writeQueue.then(async () => {
    const tmp = `${DB_FILE}.${process.pid}.tmp`;
    await fsp.writeFile(tmp, snapshot);
    await fsp.rename(tmp, DB_FILE);
  }).catch((err) => {
    console.error('[luc] failed to persist registry:', err);
  });
  return writeQueue;
}

/* ------------------------------------------------------------------ */
/* helpers                                                             */
/* ------------------------------------------------------------------ */

function json(res, status, body, extraHeaders) {
  const payload = JSON.stringify(body);
  res.writeHead(status, {
    'Content-Type': 'application/json; charset=utf-8',
    'Content-Length': Buffer.byteLength(payload),
    'Cache-Control': 'no-store',
    'Access-Control-Allow-Origin': '*',
    ...extraHeaders
  });
  res.end(payload);
}

function fail(res, status, message, fields) {
  json(res, status, { error: message, fields: fields || {} });
}

function readBody(req) {
  return new Promise((resolve, reject) => {
    let size = 0;
    const chunks = [];
    req.on('data', (chunk) => {
      size += chunk.length;
      if (size > MAX_BODY) {
        reject(Object.assign(new Error('Request body is too large.'), { status: 413 }));
        req.destroy();
        return;
      }
      chunks.push(chunk);
    });
    req.on('end', () => {
      const raw = Buffer.concat(chunks).toString('utf8');
      if (!raw) return resolve({});
      try {
        resolve(JSON.parse(raw));
      } catch {
        reject(Object.assign(new Error('Body must be valid JSON.'), { status: 400 }));
      }
    });
    req.on('error', reject);
  });
}

function cleanText(value, max) {
  if (typeof value !== 'string') return '';
  return value.replace(/\r\n/g, '\n').trim().slice(0, max);
}

function isHttpUrl(value) {
  if (!value) return true;
  try {
    const u = new URL(value);
    return u.protocol === 'http:' || u.protocol === 'https:';
  } catch {
    return false;
  }
}

function slugify(name) {
  return name
    .toLowerCase()
    .trim()
    .replace(/[^a-z0-9-]+/g, '-')
    .replace(/-+/g, '-')
    .replace(/^-|-$/g, '');
}

function publicPackage(pkg) {
  const { ownerKey, ...rest } = pkg;
  return rest;
}

function stats(pkgs) {
  const authors = new Set(pkgs.map((p) => p.author.handle));
  return {
    packages: pkgs.length,
    weeklyDownloads: pkgs.reduce((sum, p) => sum + p.weeklyDownloads, 0),
    authors: authors.size,
    licenses: new Set(pkgs.map((p) => p.license)).size,
    updatedAt: new Date().toISOString()
  };
}

function validatePublish(input, existingSlug) {
  const fields = {};
  const name = cleanText(input.name, 64);
  const slug = slugify(name);
  if (!name) fields.name = 'Pick a package name.';
  else if (!/^[a-z][a-z0-9-]{1,38}[a-z0-9]$/.test(slug)) {
    fields.name = 'Use 3–40 characters: lowercase letters, numbers and hyphens. Start with a letter.';
  } else if (!existingSlug && db.some((p) => p.slug === slug)) {
    fields.name = `"${slug}" is already taken. Try another name.`;
  }

  const summary = cleanText(input.summary, 220);
  if (summary.length < 12) fields.summary = 'Write at least a sentence (12+ characters).';
  if (summary.length > 220) fields.summary = 'Keep the summary under 220 characters.';

  const version = cleanText(input.version, 32) || '0.1.0';
  if (!/^\d+\.\d+\.\d+(-[0-9A-Za-z.-]+)?$/.test(version)) {
    fields.version = 'Use semver, for example 0.1.0 or 1.0.0-beta.1.';
  }

  const license = cleanText(input.license, 40) || 'Apache-2.0';
  if (!LICENSES.includes(license) && license !== 'Other') {
    fields.license = 'Choose a license from the list.';
  }

  const repo = cleanText(input.repo, 300);
  if (repo && !isHttpUrl(repo)) fields.repo = 'Repository must be an http(s) URL.';

  const homepage = cleanText(input.homepage, 300);
  if (homepage && !isHttpUrl(homepage)) fields.homepage = 'Homepage must be an http(s) URL.';

  const authorName = cleanText(input.authorName || (input.author && input.author.name), 60);
  if (!authorName) fields.authorName = 'Add a name or handle.';

  const authorUrl = cleanText(input.authorUrl || (input.author && input.author.url), 300);
  if (authorUrl && !isHttpUrl(authorUrl)) fields.authorUrl = 'Author link must be an http(s) URL.';

  const readme = cleanText(input.readme, 60000);
  const tags = Array.isArray(input.tags)
    ? [...new Set(input.tags.map((t) => cleanText(String(t), 24).toLowerCase()).filter(Boolean))].slice(0, 6)
    : [];

  let dependencies = {};
  if (input.dependencies && typeof input.dependencies === 'string') {
    for (const chunk of input.dependencies.split(',')) {
      const [depName, depVer] = chunk.split('@').map((s) => cleanText(s, 40));
      if (depName) dependencies[depName] = depVer ? `^${depVer.replace(/^\^/, '')}` : '*';
    }
  } else if (input.dependencies && typeof input.dependencies === 'object') {
    for (const [depName, depVer] of Object.entries(input.dependencies).slice(0, 12)) {
      const key = cleanText(depName, 40);
      if (key) dependencies[key] = cleanText(String(depVer), 20) || '*';
    }
  }

  return {
    fields,
    ok: Object.keys(fields).length === 0,
    value: { name: slug, slug, summary, version, license, repo, homepage, authorName, authorUrl, readme, tags, dependencies }
  };
}

/* ------------------------------------------------------------------ */
/* API                                                                 */
/* ------------------------------------------------------------------ */

async function handleApi(req, res, url) {
  const parts = url.pathname.split('/').filter(Boolean); // api, packages, ...

  if (req.method === 'OPTIONS') {
    res.writeHead(204, {
      'Access-Control-Allow-Origin': '*',
      'Access-Control-Allow-Methods': 'GET, POST, OPTIONS',
      'Access-Control-Allow-Headers': 'Content-Type'
    });
    return res.end();
  }

  if (parts[1] === 'stats' && req.method === 'GET') {
    return json(res, 200, stats(db));
  }

  if (parts[1] === 'licenses' && req.method === 'GET') {
    return json(res, 200, { licenses: LICENSES });
  }

  if (parts[1] !== 'packages') return fail(res, 404, 'Unknown API route.');

  /* GET /api/packages */
  if (parts.length === 2 && req.method === 'GET') {
    let list = db.slice();
    const q = (url.searchParams.get('q') || '').trim().toLowerCase();
    const tag = (url.searchParams.get('tag') || '').trim().toLowerCase();
    const sort = url.searchParams.get('sort') || 'downloads';

    if (q) {
      list = list.filter((p) =>
        [p.name, p.summary, p.author.name, p.author.handle, ...(p.tags || [])]
          .join(' ')
          .toLowerCase()
          .includes(q)
      );
    }
    if (tag) list = list.filter((p) => p.tags.some((t) => t.toLowerCase() === tag));

    if (sort === 'downloads') list.sort((a, b) => b.weeklyDownloads - a.weeklyDownloads);
    else if (sort === 'recent') list.sort((a, b) => String(b.updatedAt).localeCompare(String(a.updatedAt)));
    else if (sort === 'name') list.sort((a, b) => a.name.localeCompare(b.name));
    else if (sort === 'new') list.sort((a, b) => String(b.publishedAt).localeCompare(String(a.publishedAt)));

    return json(res, 200, {
      packages: list.map(publicPackage),
      stats: stats(db),
      total: list.length,
      query: { q, tag, sort }
    });
  }

  /* POST /api/packages */
  if (parts.length === 2 && req.method === 'POST') {
    const body = await readBody(req);
    const check = validatePublish(body);
    if (!check.ok) return fail(res, 422, 'Please fix the highlighted fields.', check.fields);

    const { value } = check;
    const today = new Date().toISOString().slice(0, 10);
    const accent = ACCENTS[db.length % ACCENTS.length];
    const pkg = normalize({
      slug: value.slug,
      name: value.name,
      importName: value.name.replace(/-/g, '_'),
      summary: value.summary,
      version: value.version,
      license: value.license,
      repo: value.repo,
      homepage: value.homepage,
      author: {
        name: value.authorName,
        handle: valHandle(value.authorName),
        url: value.authorUrl,
        verified: false
      },
      official: false,
      featured: false,
      tags: value.tags,
      accent,
      size: cleanText(body.size, 24) || '—',
      readme: value.readme,
      weeklyDownloads: 0,
      totalDownloads: 0,
      downloadsHistory: [0, 0, 0, 0, 0, 0, 0],
      publishedAt: today,
      updatedAt: today,
      dependencies: value.dependencies,
      versions: [{ version: value.version, date: today, notes: 'First release published from the website.' }],
      ownerKey: crypto.randomUUID()
    });

    db.push(pkg);
    await persist();
    return json(res, 201, { package: publicPackage(pkg), message: `Published ${pkg.name}@${pkg.version}.` });
  }

  const slug = parts[2] ? decodeURIComponent(parts[2]).toLowerCase() : '';
  const pkg = db.find((p) => p.slug === slug);

  /* GET /api/packages/:slug */
  if (parts.length === 3 && req.method === 'GET') {
    if (!pkg) return fail(res, 404, `No package named "${slug}".`);
    return json(res, 200, { package: publicPackage(pkg) });
  }

  /* POST /api/packages/:slug/install — bump counters when an install is copied */
  if (parts.length === 4 && parts[3] === 'install' && req.method === 'POST') {
    if (!pkg) return fail(res, 404, `No package named "${slug}".`);
    pkg.totalDownloads += 1;
    pkg.weeklyDownloads += 1;
    pkg.downloadsHistory[pkg.downloadsHistory.length - 1] += 1;
    await persist();
    return json(res, 200, {
      slug: pkg.slug,
      weeklyDownloads: pkg.weeklyDownloads,
      totalDownloads: pkg.totalDownloads,
      downloadsHistory: pkg.downloadsHistory
    });
  }

  /* GET /api/packages/:slug/install — the exact install recipe */
  if (parts.length === 4 && parts[3] === 'install' && req.method === 'GET') {
    if (!pkg) return fail(res, 404, `No package named "${slug}".`);
    const origin = `${req.headers['x-forwarded-proto'] === 'https' || req.socket.encrypted ? 'https' : 'http'}://${req.headers.host}`;
    const command = `luc pkg add ${pkg.name} --from ${origin}/api/packages/${pkg.slug}`;
    res.writeHead(200, {
      'Content-Type': 'text/plain; charset=utf-8',
      'Cache-Control': 'no-store',
      'Access-Control-Allow-Origin': '*'
    });
    return res.end(`${command}\n`);
  }

  return fail(res, 404, 'Unknown API route.');
}

function valHandle(name) {
  return name.toLowerCase().replace(/[^a-z0-9_-]+/g, '').slice(0, 32) || 'anonymous';
}

/* ------------------------------------------------------------------ */
/* static files                                                       */
/* ------------------------------------------------------------------ */

async function serveStatic(req, res, url) {
  let pathname = decodeURIComponent(url.pathname);
  if (pathname === '/' || pathname === '') pathname = '/index.html';

  const safePath = path.normalize(path.join(PUBLIC_DIR, pathname));
  if (!safePath.startsWith(PUBLIC_DIR + path.sep) && safePath !== path.join(PUBLIC_DIR, 'index.html')) {
    return fail(res, 403, 'Forbidden.');
  }

  let stat = null;
  try {
    stat = await fsp.stat(safePath);
  } catch {
    stat = null;
  }

  if (stat && stat.isDirectory()) {
    return serveStatic(req, res, new URL(`${pathname.replace(/\/$/, '')}/index.html`, url));
  }

  if (!stat || !stat.isFile()) {
    // SPA fallback for extension-less paths such as /about
    if (!path.extname(pathname)) {
      return sendFile(req, res, path.join(PUBLIC_DIR, 'index.html'), true);
    }
    res.writeHead(404, { 'Content-Type': 'text/plain; charset=utf-8' });
    return res.end('Not found');
  }

  return sendFile(req, res, safePath, false);
}

async function sendFile(req, res, file, spa) {
  try {
    const data = await fsp.readFile(file);
    const ext = path.extname(file).toLowerCase();
    const headers = {
      'Content-Type': MIME[ext] || 'application/octet-stream',
      'Content-Length': data.length,
      'X-Content-Type-Options': 'nosniff'
    };
    if (spa) headers['Cache-Control'] = 'no-cache';
    else if (ext === '.html') headers['Cache-Control'] = 'no-cache';
    else headers['Cache-Control'] = 'public, max-age=300';
    res.writeHead(200, headers);
    if (req.method === 'HEAD') return res.end();
    res.end(data);
  } catch (err) {
    console.error('[luc] failed to read', file, err.message);
    res.writeHead(500, { 'Content-Type': 'text/plain; charset=utf-8' });
    res.end('Server error');
  }
}

/* ------------------------------------------------------------------ */
/* server                                                              */
/* ------------------------------------------------------------------ */

const server = http.createServer(async (req, res) => {
  const started = Date.now();
  let url;
  try {
    url = new URL(req.url, `http://${req.headers.host || 'localhost'}`);
  } catch {
    return fail(res, 400, 'Bad request.');
  }

  res.on('finish', () => {
    if (process.env.LUC_SITE_LOG !== '0') {
      const ms = Date.now() - started;
      console.log(`${req.method} ${res.statusCode} ${url.pathname}${url.search} ${ms}ms`);
    }
  });

  try {
    if (url.pathname.startsWith('/api/')) return await handleApi(req, res, url);
    if (req.method !== 'GET' && req.method !== 'HEAD') return fail(res, 405, 'Method not allowed.');
    return await serveStatic(req, res, url);
  } catch (err) {
    console.error('[luc] request error:', err);
    if (res.headersSent) return res.end();
    return fail(res, err.status || 500, err.status ? err.message : 'Something went wrong on the server.');
  }
});

loadDb().then(() => {
  server.listen(PORT, HOST, () => {
    console.log(`[luc] website running at http://${HOST}:${PORT}`);
  });
});

function shutdown(signal) {
  console.log(`[luc] ${signal} received, shutting down`);
  server.close(() => process.exit(0));
  setTimeout(() => process.exit(0), 3000).unref();
}
process.on('SIGINT', () => shutdown('SIGINT'));
process.on('SIGTERM', () => shutdown('SIGTERM'));
