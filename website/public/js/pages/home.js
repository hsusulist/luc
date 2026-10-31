import { icon } from '../lib/icons.js';
import { highlight, escapeHtml } from '../lib/highlight.js';
import {
  codeBlock,
  commandBox,
  packageCard,
  sectionHead,
  observeReveal,
  formatNumber
} from '../lib/ui.js';

const heroSnippet = `!strict
import window("w")

create function greet(name)
    return "Hello, " + name + "!"
end

create x, vx = 100, 200

w.go(greet("world"), 800, 600, function(dt)
    x += vx * dt
    if x > 760 or x < 20 then vx = -vx end
    w.clear("navy")
    w.circle(x, 300, 30, "yellow")
end)`;

function numbered(code) {
  return highlight(code)
    .split('\n')
    .map((line, index) => `<span class="code-line" data-line="${index + 1}">${line || ' '}</span>`)
    .join('\n');
}

export function renderHome() {
  return {
    title: 'LUC — a small language with big batteries',
    description: 'LUC is a fast, lightweight scripting language — easy like Lua, with windows, HTTP, AI and packages built in.',
    html: `
      <section class="hero">
        <div class="container">
          <div class="hero-grid">
            <div class="hero-copy">
              <p class="eyebrow reveal"><span class="eyebrow-dot"></span> v0.2.0-beta-1 · package registry is live</p>
              <h1 class="reveal" style="--d:60ms">Code that reads<br />like <span class="grad">a sentence.</span></h1>
              <p class="hero-lede reveal" style="--d:120ms">
                LUC is a fast, lightweight scripting language — <strong>easy like Lua</strong>,
                with windows, HTTP, AI and a real package registry built in.
                One small C99 binary. No setup archaeology.
              </p>
              <div class="hero-actions reveal" style="--d:180ms">
                <a class="btn btn-primary btn-lg" href="#/download">
                  ${icon('windows', { size: 17 })} Download for Windows
                </a>
                <a class="btn btn-ghost btn-lg" href="#/guide">
                  ${icon('book', { size: 17 })} Read the guide
                </a>
                <a class="link-arrow" href="#/packages">Browse packages ${icon('arrow-right', { size: 15, cls: 'icon-arrow' })}</a>
              </div>
              <div class="hero-trust reveal" style="--d:240ms">
                <span>${icon('check', { size: 14 })} Apache 2.0</span>
                <span>${icon('check', { size: 14 })} Windows &amp; Linux</span>
                <span>${icon('check', { size: 14 })} No runtime dependencies</span>
              </div>
            </div>

            <div class="hero-visual reveal" style="--d:160ms">
              <div class="float-chip float-chip-top"><span class="dot-live" style="width:6px;height:6px;border-radius:50%;background:var(--green);box-shadow:0 0 8px var(--green)"></span> v0.2.0-beta-1</div>
              <div class="hero-window window">
                <div class="window-bar">
                  <span class="window-dots" aria-hidden="true"><i></i><i></i><i></i></span>
                  <span class="window-file">hello.luc <span class="window-live">●</span></span>
                  <button class="copy-btn" type="button" data-copy aria-label="Copy hello.luc">
                    ${icon('copy', { size: 14 })}<span>Copy</span>
                  </button>
                </div>
                <pre class="window-body"><code>${numbered(heroSnippet)}</code></pre>
                <div class="window-output">
                  <span class="out-cmd">$ luc hello.luc</span><br />
                  <span class="out-line">Hello, world! · window opened at 800×600</span>
                </div>
              </div>
              <div class="float-chip float-chip-bottom">${icon('zap', { size: 13, cls: 'chip-icon' })} <strong>1 file</strong> · starts in ~2ms</div>
            </div>
          </div>

          <div class="stats reveal" style="--d:260ms">
            <div class="stat">
              <div class="stat-value">0.2.0 <small>beta&nbsp;1</small></div>
              <div class="stat-label">Latest release</div>
            </div>
            <div class="stat">
              <div class="stat-value">C99 <small>single binary</small></div>
              <div class="stat-label">Built with</div>
            </div>
            <div class="stat">
              <div class="stat-value">Apache <small>2.0</small></div>
              <div class="stat-label">License</div>
            </div>
            <div class="stat">
              <div class="stat-value">0 <small>runtime deps</small></div>
              <div class="stat-label">Install friction</div>
            </div>
          </div>
        </div>
      </section>

      <section class="section">
        <div class="container">
          ${sectionHead({
            eyebrow: 'Why LUC',
            title: 'A small language that<br />takes the boring parts.',
            lede: 'Everything you reach for on day one is already in the interpreter. Everything else lives in a package you can read in one sitting.'
          })}

          <div class="bento">
            <article class="bento-card span-8 bento-wide reveal" data-spotlight>
              <div class="bento-wide-copy">
                <span class="card-icon">${icon('sparkles', { size: 20 })}</span>
                <h3>English, not archaeology</h3>
                <p>Declare with <code>create</code>. Repeat with <code>repeat</code>. Procedures are <code>command</code>s. Blocks end with <code>end</code>. That is most of the language — the rest you can guess.</p>
                <ul class="feature-list">
                  <li>${icon('check', { size: 14 })} No semicolons, no braces, no ceremony</li>
                  <li>${icon('check', { size: 14 })} Lists and dicts are first-class values</li>
                  <li>${icon('check', { size: 14 })} Errors point at the line that caused them</li>
                </ul>
                <div class="card-foot">
                  <a class="link-arrow" href="#/guide">Learn it in an afternoon ${icon('arrow-right', { size: 14 })}</a>
                </div>
              </div>
              <div class="mini-code"><span class="tok-comment">-- reads left to right</span>
<span class="tok-keyword">create</span> people = [<span class="tok-string">"an"</span>, <span class="tok-string">"linh"</span>, <span class="tok-string">"minh"</span>]

<span class="tok-keyword">repeat</span> person, i <span class="tok-keyword">in</span> people <span class="tok-keyword">do</span>
    print(i + <span class="tok-string">". "</span> + person)
<span class="tok-keyword">end</span>

<span class="tok-keyword">create</span> scores = {<span class="tok-string">"an"</span>: <span class="tok-number">12</span>, <span class="tok-string">"linh"</span>: <span class="tok-number">18</span>}
print(scores.an * <span class="tok-number">2</span>)

<span class="tok-comment">-- 1. an  2. linh  3. minh</span>
<span class="tok-comment">-- 24</span></div>
            </article>

            <article class="bento-card span-4 reveal" style="--d:60ms" data-spotlight>
              <span class="card-icon violet">${icon('layers', { size: 20 })}</span>
              <h3>Batteries in the box</h3>
              <p>One import from a window, an HTTP request, a Discord bot or a trainable language model.</p>
              <div class="pkg-tags" style="margin-top:14px">
                <span class="tag">window</span><span class="tag">net</span><span class="tag">ai</span><span class="tag">discord</span><span class="tag">json</span><span class="tag">buffer</span>
              </div>
              <div class="mini-code" style="margin-top:14px"><span class="tok-keyword">import</span> window(<span class="tok-string">"w"</span>)
<span class="tok-keyword">import</span> net(<span class="tok-string">"n"</span>)
<span class="tok-keyword">import</span> ai
<span class="tok-keyword">import</span> discord(<span class="tok-string">"bot"</span>)</div>
              <div class="card-foot">
                <a class="link-arrow" href="#/packages?tag=graphics">Explore libraries ${icon('arrow-right', { size: 14 })}</a>
              </div>
            </article>

            <article class="bento-card span-4 reveal" style="--d:100ms" data-spotlight>
              <span class="card-icon green">${icon('package', { size: 20 })}</span>
              <h3>Libraries that ship as one file</h3>
              <p>Mark a file with <code>make</code>, bundle parts with <code>pack</code>, share a single <code>.luic</code>. No path juggling, no manifest.</p>
              <div class="mini-code"><span class="tok-keyword">import</span> libs
<span class="tok-keyword">get</span> add, sub
<span class="tok-keyword">pack</span> mymath</div>
              <div class="card-foot">
                <a class="link-arrow" href="#/packages">See the registry ${icon('arrow-right', { size: 14 })}</a>
              </div>
            </article>

            <article class="bento-card span-4 reveal" style="--d:140ms" data-spotlight>
              <span class="card-icon blue">${icon('command', { size: 20 })}</span>
              <h3>Functions and commands</h3>
              <p><code>create function</code> returns a value. <code>command</code> runs for its effect. The compiler keeps you honest about which is which.</p>
              <div class="mini-code"><span class="tok-keyword">create function</span> add(a, b)
    <span class="tok-keyword">return</span> a + b
<span class="tok-keyword">end</span>

<span class="tok-keyword">command</span> hello(name)
    print(<span class="tok-string">"hi "</span> + name)
<span class="tok-keyword">end</span></div>
            </article>

            <article class="bento-card span-4 reveal" style="--d:180ms" data-spotlight>
              <span class="card-icon">${icon('zap', { size: 20 })}</span>
              <h3>Tiny binary, fast start</h3>
              <p>A bytecode VM written in C99, with an optional JIT for numeric loops. The whole interpreter is smaller than most node_modules folders.</p>
              <div class="mini-code"><span class="tok-comment">-- ~2ms cold start</span>
<span class="tok-comment">-- no VM, no daemon, no config</span>
$ luc hello.luc</div>
              <ul class="feature-list">
                <li>${icon('check', { size: 13 })} Bytecode VM with inline caches</li>
                <li>${icon('check', { size: 13 })} JIT for numeric hot loops</li>
              </ul>
            </article>
          </div>
        </div>
      </section>

      <section class="section section-tight">
        <div class="container">
          <div class="split">
            <div>
              ${sectionHead({
                eyebrow: 'A quick tour',
                title: 'The whole language fits on a screen.',
                lede: 'You already know how to read it. Variables, lists, loops, functions, imports — and a strict mode when you want the compiler watching.'
              })}
              <ul class="feature-list" style="gap:12px">
                <li>${icon('check', { size: 15 })} <span><strong>Strict when you want it.</strong> Add <code>!strict</code> and globals, unquoted keys and sloppy returns become compile errors.</span></li>
                <li>${icon('check', { size: 15 })} <span><strong>Exact big integers.</strong> Add <code>!karatsuba</code> and integer-string multiplication is exact.</span></li>
                <li>${icon('check', { size: 15 })} <span><strong>Coroutines and tasks.</strong> <code>task.spawn</code> and <code>task.wait</code> are built into the runtime.</span></li>
              </ul>
              <div class="hero-actions" style="margin-top:24px">
                <a class="btn btn-soft" href="#/about">How LUC works ${icon('arrow-right', { size: 15 })}</a>
              </div>
            </div>
            <div class="split-code reveal" style="--d:80ms">
              ${codeBlock(`!strict

import json
import net("n")

create function fetch_user(id)
    create body, code = n.get("https://api.example.com/u/" + id)
    if code != 200 then
        error("user " + id + " not found")
    end
    return json.decode(body)
end

create user = fetch_user("42")
print("Hello, " + user.name)

repeat tag, i in user.tags do
    print(i + ". #" + tag)
end`, { filename: 'tour.luc', lineNumbers: true, badge: 'valid LUC' })}
            </div>
          </div>
        </div>
      </section>

      <section class="section">
        <div class="container">
          <div class="featured-head">
            ${sectionHead({
              eyebrow: 'The registry',
              title: 'Packages that do the heavy lifting.',
              lede: 'Install with one command. Read the source. Publish your own before your coffee gets cold.'
            })}
            <a class="btn btn-ghost" href="#/packages">Browse all packages ${icon('arrow-right', { size: 15, cls: 'icon-arrow' })}</a>
          </div>
          <div class="pkg-grid" id="featured-packages" aria-live="polite">
            <div class="skeleton"></div><div class="skeleton"></div><div class="skeleton"></div>
          </div>
          <div class="section-actions">
            <a class="btn btn-soft" href="#/publish">${icon('upload', { size: 15 })} Publish your library</a>
          </div>
        </div>
      </section>

      <section class="section section-tight">
        <div class="container">
          <div class="cta-band reveal">
            <div>
              <p class="eyebrow" style="margin-bottom:10px"><span class="eyebrow-dot"></span> Start in 60 seconds</p>
              <h2>Ship something today.<br />The language will keep up.</h2>
              <p>Install the launcher, run the REPL, and open a window before the next song ends. No account, no IDE, no build step.</p>
              <div class="cta-band-actions">
                <a class="btn btn-primary" href="#/download">${icon('download', { size: 16 })} Download LUC</a>
                <a class="btn btn-ghost" href="#/guide">${icon('book', { size: 16 })} Beginner guide</a>
              </div>
            </div>
            <div class="cta-band-code">
              ${commandBox('npm install -g @hsusulist/luc', { label: 'Install', note: 'Installs the launcher and runtime. Windows and Linux.' })}
              ${commandBox('luc hello.luc', { label: 'Run', note: 'Need a hello.luc? The guide has you covered.' })}
            </div>
          </div>
        </div>
      </section>
    `,
    async after() {
      observeReveal();
      try {
        const res = await fetch('/api/packages?sort=downloads');
        if (!res.ok) throw new Error('bad status');
        const data = await res.json();
        const list = data.packages.filter((p) => p.featured).slice(0, 3);
        const packages = list.length ? list : data.packages.slice(0, 3);
        const wrap = document.getElementById('featured-packages');
        if (wrap) {
          wrap.innerHTML = packages.map(packageCard).join('');
          const summary = document.createElement('p');
          summary.className = 'download-note';
          summary.innerHTML = `${icon('package', { size: 13 })} ${formatNumber(data.stats.packages)} packages · ${formatNumber(data.stats.weeklyDownloads, true)} installs a week · updated live from this page`;
          wrap.insertAdjacentElement('afterend', summary);
        }
      } catch {
        const wrap = document.getElementById('featured-packages');
        if (wrap) {
          wrap.innerHTML = `
            <div class="empty-state" style="grid-column:1/-1">
              <span class="empty-icon">${icon('alert', { size: 24 })}</span>
              <h3>The registry is taking a nap</h3>
              <p>We could not reach the package API. Everything is still here — try refreshing, or browse the source directly.</p>
              <a class="btn btn-ghost" href="https://github.com/hsusulist/luc" target="_blank" rel="noopener noreferrer">${icon('github', { size: 15 })} Open GitHub</a>
            </div>`;
        }
      }
    }
  };
}
