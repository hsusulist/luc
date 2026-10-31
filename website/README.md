# LUC website

The official site for LUC: a fast, lightweight scripting language. It ships as a
zero-dependency Node service — a static front end plus a small registry API that
publishes and serves community packages.

## Run it

Requires Node 18 or newer. There is nothing to install.

```sh
cd website
node server.js
```

Open <http://localhost:3000>. Set `PORT` or `HOST` if you need to bind
differently (the default is `0.0.0.0:3000`, so it also works in a container or
preview sandbox).

## Pages

| Route | What it is |
| --- | --- |
| `#/` | Home: language pitch, a live-looking example, built-ins, registry preview |
| `#/about` | Why LUC exists, the design principles, a syntax tour, comparison |
| `#/download` | Windows installer, portable zip, npm, PowerShell script, command builder, checksums, Linux build, editor setup |
| `#/guide` | 15-chapter beginner guide with a sticky table of contents and scroll progress |
| `#/community` | Repo, discussions, issues, demo gallery, contributing flow, roadmap |
| `#/packages` | Searchable registry with tag filters and sorting |
| `#/packages/:slug` | Package page: metadata, README, install line, weekly downloads, checksums, versions |
| `#/publish` | Live-preview publishing form with validation |

Everything is a hash route, so the site works from any static file server too.

## The registry

Packages are seeded from `data/seed.js` on first boot and stored in
`data/packages.json` (generated, git-ignored). Delete that file to reseed.

Each package carries a name, summary, version, license, repo/homepage, author,
tags, dependencies, size, weekly/total downloads, version history, an optional
README (Markdown is rendered on the package page), and the install command:

```
luc pkg add <name> --from http://localhost:3000/api/packages/<slug>
```

The page exposes that command with a copy button and counts the copy as an
install. The **Publish** form posts straight to the API; there is no build step
and no account.

> The registry front end is complete, but `luc pkg add` is the registry's install
> convention — the current v0.2.0 interpreter ships `luc install window|ai|discord`.
> Treat the API as the contract for the upcoming package manager.

### API

| Method | Endpoint | Purpose |
| --- | --- | --- |
| `GET` | `/api/packages?q=&tag=&sort=downloads\|recent\|new\|name` | List/search packages |
| `GET` | `/api/packages/:slug` | One package |
| `POST` | `/api/packages` | Publish; returns `201`, or `422 { errors: { field: message } }` |
| `GET` | `/api/packages/:slug/install` | Install metadata |
| `POST` | `/api/packages/:slug/install` | Record an install copy, returns updated counts |
| `GET` | `/api/stats` | Registry totals |
| `GET` | `/api/licenses` | Accepted license ids |

The API is intentionally unauthenticated and CORS-open: this is a self-contained
demo registry, not a production package index. Writes are validated
(`name`, semver, summary length, URL scheme, tags, license, README size) and the
JSON file is written atomically.

## Structure

```
website/
├── server.js              # static file server + registry API
├── data/seed.js           # the ten starter packages
├── data/packages.json     # generated registry state (git-ignored)
└── public/
    ├── index.html         # shell, header, boot screen, footer
    ├── css/styles.css     # design system, components, responsive rules, light/dark themes
    ├── css/home-refresh.css # landing-page-only layout and cream/dark palette
    ├── js/app.js          # hash router + page lifecycle
    ├── js/lib/            # icons, syntax highlighting, Markdown, shared UI (copy, toast, reveal)
    ├── js/pages/          # one module per page (home-refresh.js is the landing page)
    └── assets/            # logo, social card, self-hosted woff2 fonts
```

The front end is vanilla ES modules — no framework, no bundler, no runtime
dependencies. Fonts are self-hosted (`Sora`, `Inter`, `JetBrains Mono`, SIL Open
Font License 1.1 — see `public/assets/fonts/OFL.txt`), so the site makes no
third-party requests.

The landing page is a self-contained refresh. `home-refresh.css` is scoped to
`body.home-refresh-active` (toggled by the router only on `/`), so the other
routes keep the original component system. The refreshed page supports both the
cream default and the dark theme, and shares the same header, footer, copy
handlers and package cards as the rest of the site.

## Publishing a package

Open `#/publish`, fill in a name, version, summary and your name (everything else
is optional, including the README), and submit. The package gets a page at
`#/packages/<slug>` and appears in the registry immediately. The live preview on
the right updates as you type.
