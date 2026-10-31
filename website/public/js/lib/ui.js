/* Shared UI helpers: code blocks, package cards, toasts, global chrome. */

import { icon } from './icons.js';
import { highlight, escapeHtml } from './highlight.js';

export { icon, escapeHtml };

const DAY = 86400000;

export function formatNumber(n, compact = false) {
  const value = Number(n) || 0;
  if (compact) {
    if (value >= 1000000) return `${(value / 1000000).toFixed(value % 1000000 === 0 ? 0 : 1)}M`;
    if (value >= 1000) return `${(value / 1000).toFixed(value % 1000 === 0 ? 0 : 1)}k`;
    return String(value);
  }
  return value.toLocaleString('en-US');
}

export function formatDate(input) {
  const date = new Date(input);
  if (Number.isNaN(date.getTime())) return String(input || '');
  return date.toLocaleDateString('en-US', { month: 'short', day: 'numeric', year: 'numeric' });
}

export function timeAgo(input) {
  const date = new Date(input);
  if (Number.isNaN(date.getTime())) return '';
  const diff = Date.now() - date.getTime();
  if (diff < DAY) return 'today';
  const days = Math.floor(diff / DAY);
  if (days < 7) return `${days} day${days === 1 ? '' : 's'} ago`;
  if (days < 30) return `${Math.floor(days / 7)} week${Math.floor(days / 7) === 1 ? '' : 's'} ago`;
  if (days < 365) return `${Math.floor(days / 30)} month${Math.floor(days / 30) === 1 ? '' : 's'} ago`;
  return `${Math.floor(days / 365)} year${Math.floor(days / 365) === 1 ? '' : 's'} ago`;
}

export function initials(name) {
  return String(name || '?')
    .replace(/[^a-zA-Z0-9]/g, ' ')
    .split(/\s+/)
    .filter(Boolean)
    .slice(0, 2)
    .map((part) => part[0].toUpperCase())
    .join('') || '?';
}

/* ------------------------------------------------------------------ */
/* code                                                                */
/* ------------------------------------------------------------------ */

export function codeBlock(code, options = {}) {
  const {
    lang = 'luc',
    filename = '',
    copy = true,
    lineNumbers = false,
    badge = '',
    className = ''
  } = options;

  const body = lang === 'luc' || lang === 'lua' ? highlight(code, lang) : escapeHtml(code);
  const lines = String(code).replace(/\r\n/g, '\n').split('\n');
  const numbered = lineNumbers
    ? body.split('\n').map((line, index) => `<span class="code-line" data-line="${index + 1}">${line || ' '}</span>`).join('\n')
    : body;

  return `
    <div class="codeblock ${className}" data-lang="${escapeHtml(lang)}">
      <div class="codeblock-bar">
        <span class="codeblock-dots" aria-hidden="true"><i></i><i></i><i></i></span>
        ${filename ? `<span class="codeblock-file">${escapeHtml(filename)}</span>` : `<span class="codeblock-lang">${escapeHtml(lang)}</span>`}
        ${badge ? `<span class="codeblock-badge">${badge}</span>` : ''}
        ${copy ? `<button class="copy-btn" type="button" data-copy data-label="Copy code">${icon('copy', { size: 15 })}<span>Copy</span></button>` : ''}
      </div>
      <pre class="codeblock-pre${lineNumbers ? ' numbered' : ''}"><code>${numbered}</code></pre>
      ${!lineNumbers ? `<span class="sr-only">${lines.length} lines of ${escapeHtml(lang)} code</span>` : ''}
    </div>`;
}

export function commandBox(command, options = {}) {
  const { label = 'Terminal', note = '', copy = true, size = '', install = '' } = options;
  return `
    <div class="commandbox ${size === 'lg' ? 'commandbox-lg' : ''}">
      ${label ? `<div class="commandbox-label">${escapeHtml(label)}</div>` : ''}
      <div class="commandbox-row">
        <span class="commandbox-prompt" aria-hidden="true">$</span>
        <code>${escapeHtml(command)}</code>
        ${copy ? `<button class="copy-btn copy-btn-ghost" type="button" data-copy data-copy-text="${escapeHtml(command)}" ${install ? `data-install="${escapeHtml(install)}"` : ''} data-label="Copy command">${icon('copy', { size: 15 })}<span>Copy</span></button>` : ''}
      </div>
      ${note ? `<p class="commandbox-note">${note}</p>` : ''}
    </div>`;
}

/* ------------------------------------------------------------------ */
/* packages                                                            */
/* ------------------------------------------------------------------ */

export function packageAvatar(pkg, size = 'md') {
  const accent = pkg.accent || '#ff7a18';
  return `<span class="pkg-avatar pkg-avatar-${size}" style="--accent:${escapeHtml(accent)}" aria-hidden="true">${escapeHtml(pkg.name.slice(0, 1))}</span>`;
}

export function tagList(tags, limit = 4) {
  return (tags || [])
    .slice(0, limit)
    .map((tag) => `<a class="tag" href="#/packages?tag=${encodeURIComponent(tag)}">${escapeHtml(tag)}</a>`)
    .join('');
}

export function sparkline(values, accent) {
  const list = Array.isArray(values) && values.length ? values : [1, 1, 1, 1, 1, 1, 1];
  const max = Math.max(...list, 1);
  return `<span class="spark" style="--accent:${escapeHtml(accent || '#ff7a18')}" aria-hidden="true">${list
    .map((v) => `<i style="height:${Math.max(12, Math.round((v / max) * 100))}%"></i>`)
    .join('')}</span>`;
}

export function packageCard(pkg) {
  const href = `#/packages/${encodeURIComponent(pkg.slug)}`;
  return `
    <article class="pkg-card" data-spotlight style="--accent:${escapeHtml(pkg.accent || '#ff7a18')}">
      <div class="pkg-card-glow" aria-hidden="true"></div>
      <div class="pkg-card-top">
        ${packageAvatar(pkg)}
        <div class="pkg-title-wrap">
          <h3 class="pkg-name">
            <a class="pkg-link" href="${href}">${escapeHtml(pkg.name)}</a>
            ${pkg.official ? `<span class="badge badge-official" title="Maintained by the LUC team">${icon('check', { size: 10 })} official</span>` : ''}
          </h3>
          <p class="pkg-author">by <span>@${escapeHtml(pkg.author.handle || pkg.author.name)}</span></p>
        </div>
        <span class="pkg-version">v${escapeHtml(pkg.version)}</span>
      </div>
      <p class="pkg-summary">${escapeHtml(pkg.summary)}</p>
      <div class="pkg-tags">${tagList(pkg.tags)}</div>
      <div class="pkg-foot">
        <span class="pkg-stat" title="Weekly downloads">${icon('download', { size: 14 })}${formatNumber(pkg.weeklyDownloads, true)}<span class="pkg-stat-suffix">/wk</span></span>
        <span class="pkg-stat" title="License">${icon('scale', { size: 14 })}${escapeHtml(pkg.license)}</span>
        <span class="pkg-open" aria-hidden="true">${icon('arrow-up-right', { size: 16 })}</span>
      </div>
    </article>`;
}

export function installCommand(pkg) {
  const origin = window.location.origin;
  return `luc pkg add ${pkg.name} --from ${origin}/api/packages/${pkg.slug}`;
}

/* ------------------------------------------------------------------ */
/* layout bits                                                         */
/* ------------------------------------------------------------------ */

export function pageHero({ eyebrow, title, lede, actions = '', meta = '', className = '' }) {
  return `
    <section class="page-hero ${className}">
      <div class="container">
        ${eyebrow ? `<p class="eyebrow reveal"><span class="eyebrow-dot"></span>${eyebrow}</p>` : ''}
        <h1 class="reveal" style="--d:60ms">${title}</h1>
        ${lede ? `<p class="page-lede reveal" style="--d:120ms">${lede}</p>` : ''}
        ${actions ? `<div class="hero-actions reveal" style="--d:180ms">${actions}</div>` : ''}
        ${meta ? `<div class="page-meta reveal" style="--d:240ms">${meta}</div>` : ''}
      </div>
    </section>`;
}

export function sectionHead({ eyebrow, title, lede = '', align = '' }) {
  return `
    <div class="section-head ${align ? `section-head-${align}` : ''}">
      ${eyebrow ? `<p class="eyebrow reveal"><span class="eyebrow-dot"></span>${eyebrow}</p>` : ''}
      <h2 class="reveal" style="--d:60ms">${title}</h2>
      ${lede ? `<p class="section-lede reveal" style="--d:120ms">${lede}</p>` : ''}
    </div>`;
}

export function emptyState({ iconName = 'package', title, body, action = '' }) {
  return `
    <div class="empty-state">
      <span class="empty-icon">${icon(iconName, { size: 26 })}</span>
      <h3>${title}</h3>
      <p>${body}</p>
      ${action}
    </div>`;
}

/* ------------------------------------------------------------------ */
/* toasts & clipboard                                                  */
/* ------------------------------------------------------------------ */

export function toast(message, type = 'success') {
  const root = document.getElementById('toast-root');
  if (!root) return;
  const el = document.createElement('div');
  el.className = `toast toast-${type}`;
  el.innerHTML = `<span class="toast-icon">${icon(type === 'error' ? 'alert' : type === 'info' ? 'info' : 'check', { size: 16 })}</span><span>${escapeHtml(message)}</span>`;
  root.appendChild(el);
  requestAnimationFrame(() => el.classList.add('in'));
  setTimeout(() => {
    el.classList.remove('in');
    setTimeout(() => el.remove(), 300);
  }, 3200);
}

export async function copyText(text) {
  try {
    await navigator.clipboard.writeText(text);
    return true;
  } catch {
    try {
      const area = document.createElement('textarea');
      area.value = text;
      area.setAttribute('readonly', '');
      area.style.position = 'fixed';
      area.style.opacity = '0';
      document.body.appendChild(area);
      area.select();
      const ok = document.execCommand('copy');
      area.remove();
      return ok;
    } catch {
      return false;
    }
  }
}

function flashCopyButton(button, label = 'Copied!') {
  if (button.dataset.copied === '1') return;
  const previous = button.innerHTML;
  const hadLabel = Boolean(button.querySelector('span'));
  button.dataset.copied = '1';
  button.classList.add('is-copied');
  button.innerHTML = `${icon('check', { size: 15 })}<span>${escapeHtml(label)}</span>`;
  setTimeout(() => {
    button.classList.remove('is-copied');
    button.innerHTML = previous;
    delete button.dataset.copied;
    if (!hadLabel) button.setAttribute('aria-label', 'Copy');
  }, 1900);
}

/* ------------------------------------------------------------------ */
/* global chrome                                                       */
/* ------------------------------------------------------------------ */

let revealObserver = null;
let revealScrollBound = false;
let revealTick = false;

function revealInView() {
  revealTick = false;
  const viewport = window.innerHeight || 800;
  document.querySelectorAll('.reveal:not(.in)').forEach((el) => {
    const rect = el.getBoundingClientRect();
    if (rect.top < viewport - 24 || rect.bottom < 0) {
      el.classList.add('in');
      revealObserver?.unobserve(el);
    }
  });
}

function bindRevealFallback() {
  if (revealScrollBound) return;
  revealScrollBound = true;
  const onScroll = () => {
    if (revealTick) return;
    revealTick = true;
    requestAnimationFrame(revealInView);
  };
  window.addEventListener('scroll', onScroll, { passive: true });
  window.addEventListener('resize', onScroll, { passive: true });
  window.addEventListener('hashchange', () => setTimeout(revealInView, 80));
}

export function observeReveal() {
  const elements = document.querySelectorAll('.reveal:not(.in)');
  bindRevealFallback();
  if (!('IntersectionObserver' in window)) {
    elements.forEach((el) => el.classList.add('in'));
    return;
  }
  if (!revealObserver) {
    revealObserver = new IntersectionObserver((entries) => {
      for (const entry of entries) {
        if (entry.isIntersecting) {
          entry.target.classList.add('in');
          revealObserver.unobserve(entry.target);
        }
      }
    }, { threshold: 0.08, rootMargin: '0px 0px -40px 0px' });
  }
  elements.forEach((el) => revealObserver.observe(el));
  requestAnimationFrame(revealInView);
}

export function setupChrome() {
  const header = document.getElementById('site-header');
  const menuButton = document.getElementById('menu-button');
  const nav = document.getElementById('primary-nav');
  const themeButton = document.getElementById('theme-toggle');

  const applyTheme = (theme) => {
    document.documentElement.setAttribute('data-theme', theme);
    try { localStorage.setItem('luc-theme', theme); } catch { /* ignore */ }
    if (themeButton) {
      themeButton.innerHTML = theme === 'light' ? icon('moon', { size: 18 }) : icon('sun', { size: 18 });
      themeButton.setAttribute('aria-label', theme === 'light' ? 'Switch to dark theme' : 'Switch to light theme');
      themeButton.setAttribute('title', theme === 'light' ? 'Dark mode' : 'Light mode');
    }
    const meta = document.querySelector('meta[name="theme-color"]');
    if (meta) meta.setAttribute('content', theme === 'light' ? '#f2efdf' : '#08070b');
  };

  let stored = 'light';
  try { stored = localStorage.getItem('luc-theme') || 'light'; } catch { /* ignore */ }
  applyTheme(stored === 'dark' ? 'dark' : 'light');

  themeButton?.addEventListener('click', () => {
    applyTheme(document.documentElement.getAttribute('data-theme') === 'light' ? 'dark' : 'light');
  });

  menuButton?.addEventListener('click', () => {
    const open = header.classList.toggle('menu-open');
    menuButton.setAttribute('aria-expanded', String(open));
    menuButton.innerHTML = open ? icon('x', { size: 20 }) : icon('menu', { size: 20 });
  });

  nav?.addEventListener('click', (event) => {
    if (event.target.closest('a')) {
      header.classList.remove('menu-open');
      menuButton?.setAttribute('aria-expanded', 'false');
      if (menuButton) menuButton.innerHTML = icon('menu', { size: 20 });
    }
  });

  document.addEventListener('click', (event) => {
    if (!header?.classList.contains('menu-open')) return;
    /* The menu button swaps its own icon, so event.target can already be
       detached by the time the event bubbles here. composedPath keeps the
       original ancestor chain. */
    if (!event.composedPath().includes(header)) {
      header.classList.remove('menu-open');
      menuButton?.setAttribute('aria-expanded', 'false');
      if (menuButton) menuButton.innerHTML = icon('menu', { size: 20 });
    }
  });

  window.addEventListener('scroll', () => {
    header?.classList.toggle('is-scrolled', window.scrollY > 12);
  }, { passive: true });
  header?.classList.toggle('is-scrolled', window.scrollY > 12);

  /* copy buttons (delegated so it works after page swaps) */
  document.addEventListener('click', async (event) => {
    const button = event.target.closest('[data-copy]');
    if (!button) return;
    event.preventDefault();

    let text = button.getAttribute('data-copy-text') || '';
    if (!text) {
      const host = button.closest('.codeblock, .commandbox, .window');
      text = host?.querySelector('code')?.textContent || '';
    }
    if (!text) return;

    const ok = await copyText(text);
    if (!ok) return toast('Your browser blocked clipboard access.', 'error');

    flashCopyButton(button);
    toast(button.dataset.install ? 'Install command copied — it pulls from this page.' : 'Copied to clipboard.');

    const slug = button.dataset.install;
    if (slug) {
      try {
        const res = await fetch(`/api/packages/${encodeURIComponent(slug)}/install`, { method: 'POST' });
        if (res.ok) {
          const data = await res.json();
          document.querySelectorAll(`[data-weekly="${slug}"]`).forEach((el) => {
            el.textContent = formatNumber(data.weeklyDownloads, true);
          });
          document.querySelectorAll(`[data-total="${slug}"]`).forEach((el) => {
            el.textContent = formatNumber(data.totalDownloads);
          });
        }
      } catch { /* counters are cosmetic */ }
    }
  });

  /* pointer spotlight on cards */
  document.addEventListener('pointermove', (event) => {
    const card = event.target.closest?.('[data-spotlight]');
    if (!card) return;
    const rect = card.getBoundingClientRect();
    card.style.setProperty('--mx', `${((event.clientX - rect.left) / rect.width) * 100}%`);
    card.style.setProperty('--my', `${((event.clientY - rect.top) / rect.height) * 100}%`);
  }, { passive: true });

  /* smooth anchor scrolling inside a page */
  document.addEventListener('click', (event) => {
    const anchor = event.target.closest('a[href^="#/guide#"], a[data-scroll]');
    if (anchor) {
      const id = anchor.getAttribute('data-scroll') || (anchor.getAttribute('href').split('#').pop());
      const target = document.getElementById(id);
      if (target) {
        event.preventDefault();
        const top = target.getBoundingClientRect().top + window.scrollY - 100;
        window.scrollTo({ top, behavior: 'smooth' });
        history.replaceState(null, '', `#/guide#${id}`);
      }
    }
  });

  observeReveal();
}

export function setActiveNav(route) {
  document.querySelectorAll('[data-route]').forEach((link) => {
    const active = link.dataset.route === route;
    link.classList.toggle('is-active', active);
    if (active) link.setAttribute('aria-current', 'page');
    else link.removeAttribute('aria-current');
  });
}
