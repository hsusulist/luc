import { icon } from '../lib/icons.js';
import { escapeHtml } from '../lib/highlight.js';
import { renderMarkdown } from '../lib/markdown.js';
import {
  packageCard,
  packageAvatar,
  tagList,
  installCommand,
  formatNumber,
  formatDate,
  timeAgo,
  pageHero,
  sectionHead,
  observeReveal,
  toast,
  emptyState,
  icon as uiIcon
} from '../lib/ui.js';

const LICENSES = ['Apache-2.0', 'MIT', 'BSD-2-Clause', 'BSD-3-Clause', 'GPL-3.0', 'LGPL-3.0', 'MPL-2.0', 'Unlicense', 'CC0-1.0', 'Other'];

/* ====================================================================== */
/* registry list                                                          */
/* ====================================================================== */

export function renderPackages(params = {}) {
  const initialTag = params.tag || '';

  return {
    title: 'Packages — the LUC registry',
    description: 'Browse community and official LUC libraries. Search by name, tag or author, then copy a one-line install command.',
    html: `
      ${pageHero({
        eyebrow: 'Package registry',
        title: 'Packages for<br /><span class="grad">anything you make.</span>',
        lede: 'Official libraries and things people built on a Sunday afternoon. Every package has a readme, a version, a license and an install command that pulls straight from this page.',
        actions: `
          <a class="btn btn-primary" href="#/publish">${icon('upload', { size: 16 })} Publish your package</a>
          <a class="btn btn-ghost" href="#/guide#libraries">${icon('book', { size: 16 })} How libraries work</a>`,
        meta: `
          <span>${icon('package', { size: 14 })} <span id="stat-packages">—</span> packages</span>
          <span>${icon('download', { size: 14 })} <span id="stat-weekly">—</span> installs / week</span>
          <span>${icon('users', { size: 14 })} <span id="stat-authors">—</span> authors</span>`
      })}

      <section class="section-tight">
        <div class="container">
          <div class="registry-toolbar">
            <div class="search-field">
              ${icon('search', { size: 17 })}
              <input type="search" id="pkg-search" placeholder="Search packages, tags or authors…" autocomplete="off" spellcheck="false" aria-label="Search packages" value="${escapeHtml(params.q || '')}" />
              <span class="search-kbd">/</span>
            </div>
            <div class="select-wrap">
              <select id="pkg-sort" aria-label="Sort packages">
                <option value="downloads">Most downloads</option>
                <option value="recent">Recently updated</option>
                <option value="new">Newest</option>
                <option value="name">Name A–Z</option>
              </select>
              ${icon('chevron-down', { size: 15 })}
            </div>
          </div>

          <div class="filter-row" id="pkg-tags" aria-label="Filter by tag">
            <span class="filter-label">Tags</span>
            <button class="filter-chip is-active" data-tag="" type="button">all</button>
          </div>

          <div class="registry-meta">
            <span id="pkg-count">Loading packages…</span>
            <span class="registry-live"><span class="status-dot"></span> live registry</span>
          </div>

          <div class="pkg-grid" id="pkg-results" aria-live="polite">
            <div class="skeleton"></div><div class="skeleton"></div><div class="skeleton"></div>
            <div class="skeleton"></div><div class="skeleton"></div><div class="skeleton"></div>
          </div>

          <div class="registry-cta reveal">
            <div>
              <h3>Made something useful?</h3>
              <p>Publishing takes a name, a sentence and a version. A README is optional. The install command is generated for you.</p>
            </div>
            <a class="btn btn-primary" href="#/publish">${icon('upload', { size: 16 })} Publish a package</a>
          </div>
        </div>
      </section>
    `,
    async after() {
      observeReveal();

      const results = document.getElementById('pkg-results');
      const count = document.getElementById('pkg-count');
      const tagsWrap = document.getElementById('pkg-tags');
      const search = document.getElementById('pkg-search');
      const sort = document.getElementById('pkg-sort');
      if (!results || !count || !search || !sort) return;

      let all = [];
      let state = { q: params.q || '', tag: initialTag, sort: 'downloads' };

      try {
        const res = await fetch('/api/packages');
        if (!res.ok) throw new Error('registry unavailable');
        const data = await res.json();
        all = data.packages || [];
        const stats = data.stats || {};
        const set = (id, value) => {
          const el = document.getElementById(id);
          if (el) el.textContent = value;
        };
        set('stat-packages', formatNumber(stats.packages || all.length));
        set('stat-weekly', formatNumber(stats.weeklyDownloads || 0, true));
        set('stat-authors', formatNumber(stats.authors || 0));
        buildTags();
      } catch {
        count.textContent = 'The registry could not be loaded.';
        results.innerHTML = emptyState({
          iconName: 'alert',
          title: 'Registry unavailable',
          body: 'The package API did not answer. Refresh the page, or check the repository while we catch up.',
          action: '<a class="btn btn-ghost btn-sm" href="https://github.com/hsusulist/luc" target="_blank" rel="noopener noreferrer">Open GitHub</a>'
        });
        return;
      }

      function buildTags() {
        if (!tagsWrap) return;
        const counts = new Map();
        all.forEach((pkg) => pkg.tags.forEach((tag) => counts.set(tag, (counts.get(tag) || 0) + 1)));
        const top = [...counts.entries()].sort((a, b) => b[1] - a[1]).slice(0, 9);
        tagsWrap.innerHTML = `
          <span class="filter-label">Tags</span>
          <button class="filter-chip ${state.tag ? '' : 'is-active'}" data-tag="" type="button">all</button>
          ${top.map(([tag, n]) => `<button class="filter-chip ${state.tag === tag ? 'is-active' : ''}" data-tag="${escapeHtml(tag)}" type="button">${escapeHtml(tag)} <span style="opacity:.55">${n}</span></button>`).join('')}`;
        if (state.tag && !top.some(([tag]) => tag === state.tag)) {
          tagsWrap.insertAdjacentHTML('beforeend', `<button class="filter-chip is-active" data-tag="${escapeHtml(state.tag)}" type="button">${escapeHtml(state.tag)}</button>`);
        }
      }

      function render() {
        const q = state.q.trim().toLowerCase();
        let list = all.filter((pkg) => {
          const tagOk = !state.tag || pkg.tags.some((tag) => tag.toLowerCase() === state.tag.toLowerCase());
          if (!tagOk) return false;
          if (!q) return true;
          return [pkg.name, pkg.summary, pkg.author.name, pkg.author.handle, ...pkg.tags]
            .join(' ')
            .toLowerCase()
            .includes(q);
        });

        if (state.sort === 'downloads') list.sort((a, b) => b.weeklyDownloads - a.weeklyDownloads);
        else if (state.sort === 'recent') list.sort((a, b) => String(b.updatedAt).localeCompare(String(a.updatedAt)));
        else if (state.sort === 'new') list.sort((a, b) => String(b.publishedAt).localeCompare(String(a.publishedAt)));
        else if (state.sort === 'name') list.sort((a, b) => a.name.localeCompare(b.name));

        count.innerHTML = `<strong>${list.length}</strong> ${list.length === 1 ? 'package' : 'packages'}${state.tag ? ` tagged <strong>${escapeHtml(state.tag)}</strong>` : ''}${q ? ` matching <strong>“${escapeHtml(state.q)}”</strong>` : ''} <span style="opacity:.6">· ${formatNumber(list.reduce((sum, pkg) => sum + pkg.weeklyDownloads, 0), true)} installs/week</span>`;

        results.innerHTML = list.length
          ? list.map(packageCard).join('')
          : emptyState({
            iconName: 'search',
            title: 'Nothing matched that',
            body: 'Try a shorter search, a different tag, or publish the package that is missing.',
            action: '<button class="btn btn-ghost btn-sm" type="button" id="pkg-clear">Clear filters</button>'
          });

        document.getElementById('pkg-clear')?.addEventListener('click', () => {
          state = { q: '', tag: '', sort: state.sort };
          search.value = '';
          buildTags();
          syncUrl();
          render();
        });

        updateUrl();
      }

      function updateUrl() {
        const query = new URLSearchParams();
        if (state.q) query.set('q', state.q);
        if (state.tag) query.set('tag', state.tag);
        if (state.sort !== 'downloads') query.set('sort', state.sort);
        const suffix = query.toString();
        history.replaceState(null, '', `#/packages${suffix ? `?${suffix}` : ''}`);
      }

      function syncUrl() {
        updateUrl();
      }

      let timer = null;
      search.addEventListener('input', () => {
        clearTimeout(timer);
        timer = setTimeout(() => {
          state.q = search.value;
          render();
        }, 140);
      });

      sort.addEventListener('change', () => {
        state.sort = sort.value;
        render();
      });

      tagsWrap?.addEventListener('click', (event) => {
        const chip = event.target.closest('[data-tag]');
        if (!chip) return;
        state.tag = chip.dataset.tag;
        [...tagsWrap.querySelectorAll('.filter-chip')].forEach((el) => el.classList.toggle('is-active', el === chip));
        render();
      });

      const onKey = (event) => {
        if (event.key === '/' && document.activeElement?.tagName !== 'INPUT' && document.activeElement?.tagName !== 'TEXTAREA') {
          event.preventDefault();
          search.focus();
        }
        if (event.key === 'Escape' && document.activeElement === search) {
          search.value = '';
          state.q = '';
          render();
        }
      };
      document.addEventListener('keydown', onKey);

      state.q = search.value;
      if (params.tag) state.tag = params.tag;
      render();

      return () => {
        document.removeEventListener('keydown', onKey);
        clearTimeout(timer);
      };
    }
  };
}

/* ====================================================================== */
/* package detail                                                         */
/* ====================================================================== */

export async function renderPackageDetail(params) {
  const slug = params.slug;

  let pkg;
  try {
    const res = await fetch(`/api/packages/${encodeURIComponent(slug)}`);
    if (!res.ok) throw new Error(res.status === 404 ? 'not-found' : 'error');
    const data = await res.json();
    pkg = data.package;
  } catch (error) {
    const missing = error.message === 'not-found';
    return {
      title: missing ? `Package ${slug} not found — LUC` : 'Registry unavailable — LUC',
      html: `
        <section class="section">
          <div class="container container-narrow">
            <div class="error-panel">
              <span class="empty-icon" style="margin-inline:auto">${uiIcon(missing ? 'package' : 'alert', { size: 26 })}</span>
              <h2>${missing ? `No package named “${escapeHtml(slug)}”` : 'The registry is taking a nap'}</h2>
              <p>${missing ? 'It may have been renamed, or it has not been published yet. The name you want might still be free.' : 'The package API did not answer. Try refreshing in a moment.'}</p>
              <div class="hero-actions" style="justify-content:center">
                <a class="btn btn-primary" href="#/packages">Back to packages</a>
                <a class="btn btn-ghost" href="#/publish">Publish this name</a>
              </div>
            </div>
          </div>
        </section>`,
      after() { observeReveal(); }
    };
  }

  const command = installCommand(pkg);
  const readme = renderMarkdown(pkg.readme);
  const history = pkg.downloadsHistory || [];
  const maxHistory = Math.max(...history, 1);
  const deps = Object.entries(pkg.dependencies || {});

  const repoLink = pkg.repo
    ? `<a class="meta-chip" href="${escapeHtml(pkg.repo)}" target="_blank" rel="noopener noreferrer">${icon('github', { size: 12 })} Repository ${icon('external', { size: 11 })}</a>`
    : '';
  const homepageLink = pkg.homepage
    ? `<a class="meta-chip" href="${escapeHtml(pkg.homepage)}" target="_blank" rel="noopener noreferrer">${icon('globe', { size: 12 })} Homepage</a>`
    : '';

  return {
    title: `${pkg.name} ${pkg.version} — LUC packages`,
    description: pkg.summary,
    html: `
      <section class="pkg-detail">
        <div class="container">
          <nav class="breadcrumb" aria-label="Breadcrumb">
            <a href="#/packages">packages</a>
            ${icon('chevron-right', { size: 12 })}
            <span>${escapeHtml(pkg.name)}</span>
          </nav>

          <header class="pkg-detail-head">
            ${packageAvatar(pkg, 'xl')}
            <div class="pkg-detail-title">
              <div class="pkg-detail-title-line">
                <h1>${escapeHtml(pkg.name)}</h1>
                <span class="pkg-version">v${escapeHtml(pkg.version)}</span>
                ${pkg.official ? `<span class="badge badge-official">${icon('check', { size: 10 })} official</span>` : '<span class="badge badge-new">community</span>'}
              </div>
              <p class="pkg-detail-summary">${escapeHtml(pkg.summary)}</p>
              <div class="pkg-meta-chips">
                <span class="meta-chip">${icon('scale', { size: 12 })} ${escapeHtml(pkg.license)}</span>
                <span class="meta-chip">${icon('clock', { size: 12 })} updated ${timeAgo(pkg.updatedAt)}</span>
                <span class="meta-chip">${icon('download', { size: 12 })} <span data-weekly="${escapeHtml(pkg.slug)}">${formatNumber(pkg.weeklyDownloads, true)}</span> / week</span>
                <span class="meta-chip">${icon('user', { size: 12 })} @${escapeHtml(pkg.author.handle || pkg.author.name)}</span>
                ${repoLink}
                ${homepageLink}
              </div>
            </div>
            <div class="pkg-detail-actions">
              <button class="btn btn-primary" type="button" data-copy data-copy-text="${escapeHtml(command)}" data-install="${escapeHtml(pkg.slug)}">${icon('download', { size: 16 })} Copy install</button>
              ${pkg.repo ? `<a class="btn btn-ghost" href="${escapeHtml(pkg.repo)}" target="_blank" rel="noopener noreferrer">${icon('github', { size: 16 })} Source</a>` : ''}
            </div>
          </header>

          <div class="pkg-layout">
            <div class="pkg-main">
              <div class="install-panel">
                <div class="install-panel-head">
                  <h3>${icon('terminal', { size: 16 })} Install ${escapeHtml(pkg.name)}</h3>
                  <span class="install-note">pulls from this page</span>
                </div>
                <div class="install-row">
                  <span class="prompt">$</span>
                  <code>${escapeHtml(command)}</code>
                  <button class="copy-btn" type="button" data-copy data-copy-text="${escapeHtml(command)}" data-install="${escapeHtml(pkg.slug)}">${icon('copy', { size: 13 })}<span>Copy</span></button>
                </div>
                <p class="install-foot">
                  Then use it in LUC: <code>import ${escapeHtml(pkg.importName || pkg.name)}</code>
                  ${pkg.official ? ' · maintained by the LUC team' : ` · by @${escapeHtml(pkg.author.handle || pkg.author.name)}`}
                </p>
              </div>

              <div class="readme-card">
                <div class="readme-card-head">
                  <h2>${icon('file-text', { size: 16 })} README ${pkg.readme ? '' : '<span class="badge badge-beta">empty</span>'}</h2>
                  ${pkg.repo ? `<a href="${escapeHtml(pkg.repo)}" target="_blank" rel="noopener noreferrer">view source ${icon('external', { size: 11 })}</a>` : ''}
                </div>
                ${readme
                  ? `<div class="prose" style="margin-top:18px">${readme}</div>`
                  : `<div class="empty-state" style="margin-top:18px;border:0;background:none">
                      <span class="empty-icon">${icon('file-text', { size: 24 })}</span>
                      <h3>No README yet</h3>
                      <p>The author published this package without a readme. That is allowed — the install command above and the metadata are enough to get started.</p>
                    </div>`}
              </div>
            </div>

            <aside class="pkg-sidebar">
              <div class="side-card">
                <h3>Downloads</h3>
                ${pkg.totalDownloads === 0
                  ? `<div class="chart-empty">${icon('download', { size: 18 })}<span>No installs yet — be the first.</span></div>`
                  : `<div class="download-chart" role="img" aria-label="Weekly download chart">
                      ${history.map((value) => `<span style="height:${Math.max(14, Math.round((value / maxHistory) * 100))}%" title="${formatNumber(value)} installs"></span>`).join('')}
                    </div>
                    <div class="chart-legend"><span>7 days ago</span><span>today</span></div>`}
                <div class="meta-list" style="margin-top:14px">
                  <div class="meta-row"><dt>This week</dt><dd data-weekly="${escapeHtml(pkg.slug)}">${formatNumber(pkg.weeklyDownloads)}</dd></div>
                  <div class="meta-row"><dt>All time</dt><dd data-total="${escapeHtml(pkg.slug)}">${formatNumber(pkg.totalDownloads)}</dd></div>
                </div>
              </div>

              <div class="side-card">
                <h3>Details</h3>
                <dl class="meta-list">
                  <div class="meta-row"><dt>Version</dt><dd>${escapeHtml(pkg.version)}</dd></div>
                  <div class="meta-row"><dt>License</dt><dd>${escapeHtml(pkg.license)}</dd></div>
                  <div class="meta-row"><dt>Published</dt><dd>${formatDate(pkg.publishedAt)}</dd></div>
                  <div class="meta-row"><dt>Updated</dt><dd>${formatDate(pkg.updatedAt)}</dd></div>
                  <div class="meta-row"><dt>Package size</dt><dd>${escapeHtml(pkg.size || '—')}</dd></div>
                  <div class="meta-row"><dt>Dependencies</dt><dd>${deps.length}</dd></div>
                  <div class="meta-row"><dt>Import</dt><dd>${escapeHtml(pkg.importName || pkg.name)}</dd></div>
                </dl>
              </div>

              <div class="side-card">
                <h3>Maintainer</h3>
                <div class="author-row">
                  <span class="author-avatar">${escapeHtml(pkg.author.name.slice(0, 2).toUpperCase())}</span>
                  <div>
                    <div class="author-name">${escapeHtml(pkg.author.name)}${pkg.author.verified ? ` ${icon('check', { size: 12 })}` : ''}</div>
                    <div class="author-handle">@${escapeHtml(pkg.author.handle || 'anonymous')}</div>
                  </div>
                </div>
                ${pkg.author.url ? `<a class="btn btn-ghost btn-sm btn-block" style="margin-top:14px" href="${escapeHtml(pkg.author.url)}" target="_blank" rel="noopener noreferrer">${icon('github', { size: 14 })} View profile</a>` : ''}
              </div>

              ${deps.length ? `
              <div class="side-card">
                <h3>Dependencies</h3>
                <div class="side-links">
                  ${deps.map(([name, version]) => `<a href="#/packages/${encodeURIComponent(name)}">${icon('package', { size: 14 })} ${escapeHtml(name)} <span style="margin-left:auto;font-family:var(--font-mono);font-size:10px;color:var(--muted-2)">${escapeHtml(version)}</span></a>`).join('')}
                </div>
              </div>` : ''}

              <div class="side-card">
                <h3>Versions</h3>
                <ul class="version-list">
                  ${(pkg.versions || []).map((version, index) => `
                    <li class="version-item">
                      <strong>v${escapeHtml(version.version)} ${index === 0 ? '<span class="badge badge-new" style="margin-left:4px">latest</span>' : ''}</strong>
                      <span>${formatDate(version.date)}</span>
                      ${version.notes ? `<p>${escapeHtml(version.notes)}</p>` : ''}
                    </li>`).join('') || '<li class="version-item"><strong>v' + escapeHtml(pkg.version) + '</strong><span>No changelog yet.</span></li>'}
                </ul>
              </div>

              <div class="side-card">
                <h3>Tags</h3>
                <div class="pkg-tags">${tagList(pkg.tags, 8) || '<span style="font-size:11.5px;color:var(--muted-2)">No tags</span>'}</div>
              </div>
            </aside>
          </div>
        </div>
      </section>
    `,
    after() {
      observeReveal();
    }
  };
}

/* ====================================================================== */
/* publish                                                                */
/* ====================================================================== */

function field({ id, label, hint = '', type = 'text', placeholder = '', value = '', mono = false, help = '' }) {
  return `
    <div class="field" data-field="${id}">
      <label for="${id}">${label}${hint ? `<span class="hint">${hint}</span>` : ''}</label>
      <input id="${id}" name="${id}" type="${type}" placeholder="${escapeHtml(placeholder)}" value="${escapeHtml(value)}" ${mono ? 'class="mono"' : ''} spellcheck="false" />
      ${help ? `<p class="field-help">${help}</p>` : ''}
      <p class="field-error" data-error="${id}"></p>
    </div>`;
}

export function renderPublish() {
  return {
    title: 'Publish a package — LUC registry',
    description: 'Publish a LUC library in a minute. Name it, describe it, add a readme if you like, and get a copyable install command.',
    html: `
      ${pageHero({
        eyebrow: 'Publish',
        title: 'Put your library<br /><span class="grad">on the page.</span>',
        lede: 'A name, one sentence, a version. Everything else is optional — including the README. Your install command is generated the moment you publish.',
        meta: `
          <span>${icon('clock', { size: 14 })} Takes about a minute</span>
          <span>${icon('package', { size: 14 })} Libraries, games, tools — anything</span>
          <span>${icon('scale', { size: 14 })} You choose the license</span>`
      })}

      <div class="container">
        <div class="publish-layout">
          <form class="form-card" id="publish-form" novalidate>
            <div class="form-section">
              <p class="form-section-title">The basics<span>A good name and one honest sentence.</span></p>
              <div class="form-grid">
                ${field({ id: 'name', label: 'Package name', hint: 'lowercase, hyphens', placeholder: 'mymath', mono: true, help: 'This becomes the import name and the folder on the registry page.' })}
                ${field({ id: 'version', label: 'Version', hint: 'semver', placeholder: '0.1.0', value: '0.1.0', mono: true })}
              </div>
              <div class="field" data-field="summary">
                <label for="summary">Summary <span class="hint"><span id="summary-count">0</span>/220</span></label>
                <input id="summary" name="summary" type="text" maxlength="220" placeholder="Exact big-integer helpers with a friendly API." autocomplete="off" />
                <p class="field-error" data-error="summary"></p>
              </div>
              <div class="field" data-field="license">
                <label for="license">License <span class="hint">pick one</span></label>
                <select id="license" name="license">
                  ${LICENSES.map((license) => `<option value="${license}" ${license === 'Apache-2.0' ? 'selected' : ''}>${license}</option>`).join('')}
                </select>
                <p class="field-error" data-error="license"></p>
              </div>
            </div>

            <div class="form-section">
              <p class="form-section-title">Links<span>Where people can read or follow the source.</span></p>
              <div class="form-grid">
                ${field({ id: 'repo', label: 'Repository', hint: 'optional', placeholder: 'https://github.com/you/project', type: 'url', mono: true })}
                ${field({ id: 'homepage', label: 'Homepage', hint: 'optional', placeholder: 'https://your.site/mymath', type: 'url', mono: true })}
              </div>
              <div class="form-grid">
                ${field({ id: 'authorName', label: 'Your name', hint: 'required', placeholder: 'Your name or handle' })}
                ${field({ id: 'authorUrl', label: 'Profile link', hint: 'optional', placeholder: 'https://github.com/you', type: 'url', mono: true })}
              </div>
              <div class="field" data-field="tags">
                <label for="tag-input">Tags <span class="hint">up to 6 · Enter to add</span></label>
                <div class="tag-editor" id="tag-editor">
                  <input id="tag-input" type="text" placeholder="math, tooling, ai…" autocomplete="off" />
                </div>
                <p class="field-help">Tags help people find your package. Use lowercase words.</p>
                <p class="field-error" data-error="tags"></p>
              </div>
              <div class="field" data-field="dependencies">
                <label for="dependencies">Dependencies <span class="hint">optional</span></label>
                <input id="dependencies" name="dependencies" type="text" placeholder="net@1.1.0, json@0.4.0" class="mono" autocomplete="off" />
                <p class="field-help">Comma separated, <code>name@version</code>. Leave blank if none.</p>
              </div>
            </div>

            <div class="form-section">
              <p class="form-section-title">README<span>Optional. Markdown works: headings, lists, code fences, links, tables.</span></p>
              <div class="field" data-field="readme">
                <label for="readme">Long description <span class="hint">optional · up to 60,000 characters</span></label>
                <textarea id="readme" name="readme" placeholder="# mymath&#10;&#10;Exact math helpers for LUC.&#10;&#10;Add usage, examples and an install section. Markdown is supported."></textarea>
                <p class="field-help">Leave it blank if you prefer. The package page will show a friendly “no README yet” state.</p>
                <p class="field-error" data-error="readme"></p>
              </div>
            </div>

            <div class="form-footer">
              <p>By publishing you confirm you have the right to share this code and that the license you picked applies to it.</p>
              <button class="btn btn-primary" type="submit" id="publish-submit">${icon('upload', { size: 16 })} Publish package</button>
            </div>
          </form>

          <aside class="preview-card">
            <div class="preview-label"><span class="dot-live"></span> Live preview</div>
            <div id="preview-host">
              ${packageCard({
                slug: 'preview',
                name: 'your-package',
                summary: 'Your summary will appear here. Write one clear sentence about what the package does and who it is for.',
                version: '0.1.0',
                license: 'Apache-2.0',
                weeklyDownloads: 0,
                tags: ['tag'],
                accent: '#ff7a18',
                author: { name: 'you', handle: 'you', url: '', verified: false },
                official: false
              })}
            </div>
            <div class="preview-readme" id="preview-readme">
              <div class="prose"><p style="color:var(--muted-2)">Your README preview appears here. Empty is fine.</p></div>
            </div>
            <div class="side-card" style="margin-top:14px">
              <h3>What happens on publish</h3>
              <div class="feature-list" style="margin-top:0">
                <li>${icon('check', { size: 13 })} <span>A package page with your metadata</span></li>
                <li>${icon('check', { size: 13 })} <span>A copyable install command</span></li>
                <li>${icon('check', { size: 13 })} <span>Search, tags and version history</span></li>
                <li>${icon('check', { size: 13 })} <span>No build step, no waiting</span></li>
              </div>
            </div>
          </aside>
        </div>
      </div>
    `,
    after() {
      observeReveal();

      const form = document.getElementById('publish-form');
      if (!form) return;

      const name = document.getElementById('name');
      const version = document.getElementById('version');
      const summary = document.getElementById('summary');
      const license = document.getElementById('license');
      const repo = document.getElementById('repo');
      const homepage = document.getElementById('homepage');
      const authorName = document.getElementById('authorName');
      const authorUrl = document.getElementById('authorUrl');
      const readme = document.getElementById('readme');
      const dependencies = document.getElementById('dependencies');
      const tagInput = document.getElementById('tag-input');
      const tagEditor = document.getElementById('tag-editor');
      const previewHost = document.getElementById('preview-host');
      const previewReadme = document.getElementById('preview-readme');
      const summaryCount = document.getElementById('summary-count');
      const submit = document.getElementById('publish-submit');
      const tags = [];
      let previewTimer = null;

      const clean = (value) => String(value || '').trim();

      function renderPreview() {
        const pkgName = clean(name?.value) || 'your-package';
        const pkg = {
          slug: 'preview',
          name: pkgName.toLowerCase().replace(/[^a-z0-9-]+/g, '-') || 'your-package',
          summary: clean(summary?.value) || 'Your summary will appear here. Write one clear sentence about what the package does and who it is for.',
          version: clean(version?.value) || '0.1.0',
          license: license?.value || 'Apache-2.0',
          weeklyDownloads: 0,
          tags: tags.length ? tags : ['tag'],
          accent: '#ff7a18',
          author: { name: clean(authorName?.value) || 'you', handle: clean(authorName?.value).toLowerCase().replace(/[^a-z0-9_-]+/g, '') || 'you', url: '', verified: false },
          official: false
        };
        if (previewHost) previewHost.innerHTML = packageCard(pkg);
        if (previewReadme) {
          const text = clean(readme?.value);
          previewReadme.innerHTML = text
            ? `<div class="prose">${renderMarkdown(text)}</div>`
            : '<div class="prose"><p style="color:var(--muted-2)">Your README preview appears here. Empty is fine.</p></div>';
        }
      }

      function schedulePreview() {
        clearTimeout(previewTimer);
        previewTimer = setTimeout(renderPreview, 120);
      }

      function renderTags() {
        if (!tagEditor || !tagInput) return;
        tagEditor.querySelectorAll('.tag-chip').forEach((chip) => chip.remove());
        tags.forEach((tag) => {
          const chip = document.createElement('span');
          chip.className = 'tag-chip';
          chip.innerHTML = `${escapeHtml(tag)}<button type="button" aria-label="Remove ${escapeHtml(tag)}">${icon('x', { size: 10 })}</button>`;
          chip.querySelector('button').addEventListener('click', () => {
            tags.splice(tags.indexOf(tag), 1);
            renderTags();
            renderPreview();
          });
          tagEditor.insertBefore(chip, tagInput);
        });
      }

      function addTag(raw) {
        const value = raw.toLowerCase().replace(/[^a-z0-9._-]+/g, '').slice(0, 24);
        if (value && !tags.includes(value) && tags.length < 6) {
          tags.push(value);
          renderTags();
          renderPreview();
        }
        if (tagInput) tagInput.value = '';
      }

      tagInput?.addEventListener('keydown', (event) => {
        if (event.key === 'Enter' || event.key === ',') {
          event.preventDefault();
          addTag(tagInput.value);
        } else if (event.key === 'Backspace' && !tagInput.value && tags.length) {
          tags.pop();
          renderTags();
          renderPreview();
        }
      });
      tagInput?.addEventListener('blur', () => {
        if (tagInput.value.trim()) addTag(tagInput.value);
      });

      summary?.addEventListener('input', () => {
        if (summaryCount) summaryCount.textContent = String(summary.value.length);
        schedulePreview();
      });

      /* Clear a field's error as soon as the visitor edits it. */
      form.querySelectorAll('input, textarea, select').forEach((input) => {
        const clear = () => input.closest('.field')?.classList.remove('has-error');
        input.addEventListener('input', clear);
        input.addEventListener('change', clear);
      });

      [name, version, license, authorName].forEach((input) => input?.addEventListener('input', schedulePreview));
      license?.addEventListener('change', schedulePreview);
      readme?.addEventListener('input', schedulePreview);

      const clearErrors = () => {
        form.querySelectorAll('.field.has-error').forEach((fieldEl) => fieldEl.classList.remove('has-error'));
      };

      const showErrors = (fields) => {
        Object.entries(fields).forEach(([key, message]) => {
          const wrap = form.querySelector(`[data-field="${key}"]`);
          if (!wrap) return;
          wrap.classList.add('has-error');
          const error = wrap.querySelector('.field-error');
          if (error) error.textContent = message;
        });
        const first = form.querySelector('.field.has-error');
        first?.scrollIntoView({ behavior: 'smooth', block: 'center' });
        first?.querySelector('input, textarea, select')?.focus({ preventScroll: true });
      };

      const clientCheck = () => {
        const errors = {};
        const pkgName = clean(name.value);
        if (!pkgName) errors.name = 'Pick a package name.';
        else if (!/^[a-zA-Z][a-zA-Z0-9-]{1,38}[a-zA-Z0-9]$/.test(pkgName)) errors.name = '3–40 characters: letters, numbers and hyphens, starting with a letter.';
        if (clean(summary.value).length < 12) errors.summary = 'Write at least a sentence (12+ characters).';
        if (!/^\d+\.\d+\.\d+(-[0-9A-Za-z.-]+)?$/.test(clean(version.value))) errors.version = 'Use semver, for example 0.1.0.';
        if (!clean(authorName.value)) errors.authorName = 'Add your name or handle.';
        const repoValue = clean(repo.value);
        if (repoValue && !/^https?:\/\/.+/i.test(repoValue)) errors.repo = 'Use a full http(s) URL.';
        const homepageValue = clean(homepage.value);
        if (homepageValue && !/^https?:\/\/.+/i.test(homepageValue)) errors.homepage = 'Use a full http(s) URL.';
        return errors;
      };

      form.addEventListener('submit', async (event) => {
        event.preventDefault();
        clearErrors();
        const errors = clientCheck();
        if (Object.keys(errors).length) {
          showErrors(errors);
          return;
        }

        const payload = {
          name: clean(name.value),
          summary: clean(summary.value),
          version: clean(version.value),
          license: license.value,
          repo: clean(repo.value),
          homepage: clean(homepage.value),
          authorName: clean(authorName.value),
          authorUrl: clean(authorUrl.value),
          readme: readme.value,
          tags,
          dependencies: clean(dependencies.value)
        };

        submit.disabled = true;
        const previous = submit.innerHTML;
        submit.innerHTML = `<span style="display:inline-block;width:14px;height:14px;border:2px solid rgba(0,0,0,.25);border-top-color:#1a0d02;border-radius:50%;animation:spin .7s linear infinite"></span> Publishing…`;

        try {
          const res = await fetch('/api/packages', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(payload)
          });
          const data = await res.json();

          if (!res.ok) {
            if (data.fields) showErrors(data.fields);
            toast(data.error || 'Could not publish the package.', 'error');
            submit.disabled = false;
            submit.innerHTML = previous;
            return;
          }

          const pkg = data.package;
          const command = installCommand(pkg);
          const app = document.getElementById('app');
          app.innerHTML = `
            <section class="section">
              <div class="container">
                <div class="publish-success">
                  <span class="success-mark">${icon('check', { size: 28 })}</span>
                  <h1>${escapeHtml(pkg.name)} is live.</h1>
                  <p>Version ${escapeHtml(pkg.version)} is on the registry. Copy the install line or open the package page.</p>
                  <div class="commandbox">
                    <div class="commandbox-label">Install from this page</div>
                    <div class="commandbox-row">
                      <span class="commandbox-prompt">$</span>
                      <code>${escapeHtml(command)}</code>
                      <button class="copy-btn copy-btn-ghost" type="button" data-copy data-copy-text="${escapeHtml(command)}" data-install="${escapeHtml(pkg.slug)}">${icon('copy', { size: 14 })}<span>Copy</span></button>
                    </div>
                  </div>
                  <div class="success-actions">
                    <a class="btn btn-primary" href="#/packages/${encodeURIComponent(pkg.slug)}">View package</a>
                    <a class="btn btn-ghost" href="#/publish">Publish another</a>
                    <a class="btn btn-ghost" href="#/packages">Browse registry</a>
                  </div>
                  <p style="margin-top:22px;font-size:11.5px;color:var(--muted-2)">Saved to this site's registry. You can share the package URL — the install command pulls the metadata from this page.</p>
                </div>
              </div>
            </section>`;
          window.scrollTo({ top: 0, behavior: 'smooth' });
          toast(`${pkg.name}@${pkg.version} published.`);
          observeReveal();
        } catch {
          toast('Could not reach the registry. Try again in a moment.', 'error');
          submit.disabled = false;
          submit.innerHTML = previous;
        }
      });

      renderPreview();
    }
  };
}
