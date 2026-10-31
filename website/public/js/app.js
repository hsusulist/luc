import { setupChrome, setActiveNav, observeReveal, icon } from './lib/ui.js';
import { renderHome } from './pages/home.js';
import { renderAbout } from './pages/about.js';
import { renderDownload } from './pages/download.js';
import { renderGuide } from './pages/guide.js';
import { renderCommunity } from './pages/community.js';
import { renderPackages, renderPackageDetail, renderPublish } from './pages/packages.js';

const routes = [
  {
    path: '/',
    nav: 'home',
    render: () => renderHome()
  },
  {
    path: '/about',
    nav: 'about',
    render: () => renderAbout()
  },
  {
    path: '/download',
    nav: 'download',
    render: () => renderDownload()
  },
  {
    path: '/guide',
    nav: 'guide',
    render: () => renderGuide()
  },
  {
    path: '/community',
    nav: 'community',
    render: () => renderCommunity()
  },
  {
    path: '/packages',
    nav: 'packages',
    render: (params) => renderPackages(params)
  },
  {
    path: '/publish',
    nav: 'packages',
    render: () => renderPublish()
  }
];

let cleanup = null;
let renderToken = 0;

function parseLocation() {
  const raw = window.location.hash.replace(/^#/, '') || '/';
  const [beforeQuery, queryString = ''] = raw.split('?');
  const [pathPart, ...anchorParts] = beforeQuery.split('#');
  let path = pathPart || '/';
  if (!path.startsWith('/')) path = `/${path}`;
  if (path.length > 1) path = path.replace(/\/+$/, '');
  const params = Object.fromEntries(new URLSearchParams(queryString));
  const anchor = anchorParts.join('#');
  return { path: path || '/', params, anchor };
}

function notFound(path) {
  return {
    title: 'Page not found — LUC',
    html: `
      <section class="section">
        <div class="container container-narrow">
          <div class="error-panel">
            <span class="empty-icon" style="margin-inline:auto">${icon('info', { size: 26 })}</span>
            <h2>That page wandered off.</h2>
            <p>There is no route for <code>${path.replace(/[<>&]/g, '')}</code>. Try the language, the guide, or the package registry.</p>
            <div class="hero-actions" style="justify-content:center">
              <a class="btn btn-primary" href="#/">Back home</a>
              <a class="btn btn-ghost" href="#/packages">Browse packages</a>
            </div>
          </div>
        </div>
      </section>`,
    after() { observeReveal(); }
  };
}

function resolve(path, params) {
  const exact = routes.find((route) => route.path === path);
  if (exact) return exact;

  const packageMatch = path.match(/^\/packages\/([^/]+)$/);
  if (packageMatch) {
    return {
      nav: 'packages',
      render: () => renderPackageDetail({ slug: decodeURIComponent(packageMatch[1]) })
    };
  }

  return {
    nav: '',
    render: () => notFound(path)
  };
}

async function renderRoute() {
  const token = ++renderToken;
  const { path, params, anchor } = parseLocation();
  const route = resolve(path, params);
  const app = document.getElementById('app');
  if (!app) return;

  if (cleanup) {
    try { cleanup(); } catch { /* ignore */ }
    cleanup = null;
  }

  try {
    const page = await route.render(params);
    if (token !== renderToken) return;

    app.innerHTML = page.html;
    document.title = page.title || 'LUC';
    const meta = document.querySelector('meta[name="description"]');
    if (meta && page.description) meta.setAttribute('content', page.description);

    setActiveNav(route.nav || '');
    observeReveal();

    if (page.after) {
      const result = await page.after();
      if (typeof result === 'function') cleanup = result;
    }

    app.classList.remove('page-enter');
    void app.offsetWidth;
    app.classList.add('page-enter');

    if (anchor) {
      const target = document.getElementById(anchor);
      if (target) {
        requestAnimationFrame(() => {
          const top = target.getBoundingClientRect().top + window.scrollY - 100;
          window.scrollTo({ top, behavior: 'smooth' });
        });
      }
    } else {
      window.scrollTo(0, 0);
    }
  } catch (error) {
    console.error('[luc] route failed:', error);
    if (token !== renderToken) return;
    app.innerHTML = `
      <section class="section">
        <div class="container container-narrow">
          <div class="error-panel">
            <h2>The page failed to load.</h2>
            <p>${String(error && error.message ? error.message : 'Unknown error').replace(/[<>&]/g, '')}</p>
            <div class="hero-actions" style="justify-content:center">
              <a class="btn btn-primary" href="#/">Back home</a>
            </div>
          </div>
        </div>
      </section>`;
  }
}

setupChrome();
window.addEventListener('hashchange', renderRoute);
renderRoute();

if (!window.location.hash) {
  history.replaceState(null, '', '#/');
}
