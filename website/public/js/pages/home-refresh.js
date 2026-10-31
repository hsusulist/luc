import { icon } from '../lib/icons.js';
import { highlight, escapeHtml } from '../lib/highlight.js';
import { packageCard, observeReveal, formatNumber } from '../lib/ui.js';

const heroSnippet = `!strict
import window("w")

create function greet(name)
    return "Hello, " + name + "!"
end

w.go(greet("world"), 800, 600, function(dt)
    w.clear("navy")
    w.circle(400, 300, 28, "yellow")
end)`;

function numbered(code) {
  return highlight(code)
    .split('\n')
    .map((line, index) => `<span data-line="${String(index + 1).padStart(2, '0')}">${line || '&nbsp;'}</span>`)
    .join('\n');
}

function featureCard(index, title, description, snippet, diagram, iconName) {
  return `
    <article class="lr-capability lr-cap-${diagram}">
      <div class="lr-cap-top"><span>${String(index).padStart(2, '0')}</span>${icon(iconName, { size: 22 })}</div>
      <div class="lr-${diagram}-diagram" aria-hidden="true">${diagram === 'window'
        ? '<span></span><span></span><span></span><i></i><b></b>'
        : diagram === 'http'
          ? '<span>GET</span><i>→</i><b>200</b>'
          : diagram === 'ai'
            ? '<i></i><i></i><i></i><b>AI</b>'
            : '<b>&gt;_</b><span>bot online</span><i></i>'}</div>
      <h3>${title}</h3>
      <p>${description}</p>
      <code>${snippet}</code>
    </article>`;
}

export function renderHome() {
  return {
    title: 'LUC — Less setup. More making.',
    description: 'A small C99 scripting language that reads like plain English, with windows, HTTP, AI, Discord and packages built in.',
    html: `
      <div class="luc-refresh">
        <section class="lr-hero">
          <div class="lr-hero-gridmark" aria-hidden="true">LUC / 01</div>
          <div class="lr-hero-inner">
            <div class="lr-hero-copy">
              <div class="lr-kicker"><span class="lr-kicker-line"></span>C99 scripting language <span class="lr-version">v0.2.0-beta-1</span></div>
              <h1>Less setup.<br><span>More making.</span></h1>
              <p class="lr-lede">A small language that reads like plain English. Make a window, call an API, build a bot — the useful bits are already in the box.</p>
              <div class="lr-actions">
                <a class="lr-button lr-button-orange" href="#/download">${icon('download', { size: 17 })} Download for Windows ${icon('arrow-right', { size: 16 })}</a>
                <a class="lr-text-link" href="#/guide">${icon('book', { size: 16 })} Start with the guide</a>
              </div>
              <div class="lr-proof">
                <span>${icon('check', { size: 14 })} Apache 2.0</span>
                <span>${icon('check', { size: 14 })} Windows &amp; Linux</span>
                <span>${icon('check', { size: 14 })} No runtime dependencies</span>
              </div>
              <div class="lr-hero-note"><span class="lr-note-index">01—</span>The small language for the thing you want to make today.</div>
            </div>

            <div class="lr-code-stage">
              <div class="lr-code-stamp"><span>REAL CODE</span><span>11 LINES</span></div>
              <div class="lr-code-window window">
                <div class="lr-codebar">
                  <div class="lr-file-label"><span class="lr-file-mark">L</span><span>hello.luc</span><span class="lr-file-state">●</span></div>
                  <button class="lr-copy" type="button" data-copy data-copy-text="${escapeHtml(heroSnippet)}" aria-label="Copy LUC code example">${icon('copy', { size: 15 })}<span>Copy</span></button>
                </div>
                <pre class="lr-code" aria-label="LUC code example"><code>${numbered(heroSnippet)}</code></pre>
                <div class="lr-output">
                  <span class="lr-output-label">${icon('terminal', { size: 13 })} OUTPUT</span>
                  <code><b>$</b> luc hello.luc<br><span>Hello, world! · window opened at 800×600</span></code>
                  <div class="lr-output-indicator"><i></i> RUN COMPLETE</div>
                </div>
              </div>
              <div class="lr-side-note"><span>COMPILE</span><span class="lr-side-arrow">↘</span><span>RUN</span><span class="lr-side-arrow">↘</span><span>MAKE</span></div>
            </div>
          </div>
          <div class="lr-hero-footer">
            <div class="lr-hero-footer-inner">
              <span>One small C99 interpreter.</span>
              <div class="lr-footer-signal"><i></i><i></i><i></i><i></i><i></i><b>READY WHEN YOU ARE</b></div>
              <a href="#/about">What is LUC? ${icon('arrow-right', { size: 14 })}</a>
            </div>
          </div>
        </section>

        <section class="lr-principles" aria-label="LUC at a glance">
          <div class="lr-principle-intro">
            <span class="lr-section-index">01 / THE SHORT VERSION</span>
            <h2>Small on purpose.<br><em>Capable by default.</em></h2>
          </div>
          <div class="lr-principle-list">
            <div class="lr-principle"><span class="lr-principle-no">A</span><div><strong>Plain-language syntax</strong><p>Use <code>create</code>, <code>repeat</code>, and <code>command</code>. The code tells you what it does.</p></div></div>
            <div class="lr-principle"><span class="lr-principle-no">B</span><div><strong>Built for small programs</strong><p>A compact C99 interpreter keeps the distance between idea and running code short.</p></div></div>
            <div class="lr-principle"><span class="lr-principle-no">C</span><div><strong>Useful tools included</strong><p>Windows, HTTP, AI, Discord, and a package registry are part of the story.</p></div></div>
          </div>
        </section>

        <section class="lr-builtins">
          <div class="lr-builtins-top">
            <div><span class="lr-section-index">02 / INCLUDED, NOT BOLTED ON</span><h2>Your first import<br>can do <em>something real.</em></h2></div>
            <p>Start with the capability you need. Keep the rest of your attention on the program.</p>
          </div>
          <div class="lr-capability-grid">
            ${featureCard(1, 'Windows', 'Draw a window and work directly with graphics.', 'import window("w")', 'window', 'windows')}
            ${featureCard(2, 'HTTP', 'Make requests without reaching for another runtime.', 'import net("n")', 'http', 'globe')}
            ${featureCard(3, 'AI', 'Build with a trainable language model in the runtime.', 'import ai', 'ai', 'sparkles')}
            ${featureCard(4, 'Discord', 'Bring a bot to life with a built-in integration.', 'import discord("bot")', 'discord', 'message')}
          </div>
          <div class="lr-builtins-bottom"><span>And a package registry to take you further.</span><a href="#/packages">Explore packages ${icon('arrow-right', { size: 15 })}</a></div>
        </section>

        <section class="lr-language">
          <div class="lr-language-aside">
            <span class="lr-section-index">03 / LEARN BY READING</span>
            <div class="lr-language-display">say what<br>you mean<span>.</span></div>
            <p>Fewer symbols to memorize. More code that makes sense before you have to look it up.</p>
            <a class="lr-outline-link" href="#/guide">Read the beginner guide ${icon('arrow-right', { size: 15 })}</a>
          </div>
          <div class="lr-language-example">
            <div class="lr-example-head"><span>LOOP.LUC</span><span>THE SHAPE OF LUC</span></div>
            <div class="lr-example-code">
              <div><i>01</i><b>create</b> people = [<em>"An"</em>, <em>"Linh"</em>, <em>"Minh"</em>]</div>
              <div class="lr-blank"><i>02</i></div>
              <div><i>03</i><b>repeat</b> person, i <b>in</b> people <b>do</b></div>
              <div><i>04</i>    print(i + <em>". "</em> + person)</div>
              <div><i>05</i><b>end</b></div>
            </div>
            <div class="lr-example-output"><span>THAT'S THE WHOLE LOOP</span><div><b>1.</b> An <b>2.</b> Linh <b>3.</b> Minh</div></div>
            <div class="lr-syntax-legend"><span><b>create</b> declares</span><span><b>repeat</b> iterates</span><span><b>end</b> closes a block</span></div>
          </div>
        </section>

        <section class="lr-ecosystem">
          <div class="lr-ecosystem-heading">
            <span class="lr-section-index">04 / ROOM TO GROW</span>
            <h2>One file to start.<br><em>A registry when you're ready.</em></h2>
          </div>
          <div class="lr-ecosystem-body">
            <div class="lr-registry-art" aria-hidden="true">
              <div class="lr-registry-code"><span>import</span> libraries<br><span>get</span> what you need<br><span>make</span> something yours</div>
              <div class="lr-registry-cube lr-cube-one">${icon('package', { size: 22 })}<b>PACKAGE</b><span>.luic</span></div>
              <div class="lr-registry-cube lr-cube-two">${icon('command', { size: 21 })}<b>MODULE</b><span>luc</span></div>
              <div class="lr-registry-path">LUC REGISTRY ${icon('arrow-right', { size: 15 })} YOUR PROJECT</div>
            </div>
            <div class="lr-registry-copy">
              <p>Libraries can ship as a single <code>.luic</code> file. Browse the registry, read what you install, and publish a library of your own.</p>
              <div class="lr-registry-points">
                <div><span>01</span><b>Find a package</b>${icon('arrow-up-right', { size: 16 })}</div>
                <div><span>02</span><b>Read the source</b>${icon('arrow-up-right', { size: 16 })}</div>
                <div><span>03</span><b>Share your work</b>${icon('arrow-up-right', { size: 16 })}</div>
              </div>
              <div class="lr-registry-actions">
                <a class="lr-button lr-button-cream" href="#/packages">Browse packages ${icon('arrow-right', { size: 16 })}</a>
                <a class="lr-registry-publish" href="#/publish">Publish a package ${icon('arrow-up-right', { size: 14 })}</a>
              </div>
            </div>
          </div>
          <div class="lr-featured">
            <div class="lr-featured-head"><div><span class="lr-section-index">LIVE FROM THE REGISTRY</span><h3>Packages worth a look.</h3></div><a href="#/packages">Browse all ${icon('arrow-right', { size: 15 })}</a></div>
            <div class="lr-featured-grid pkg-grid" id="featured-packages" aria-live="polite"><div class="lr-featured-loading">Loading featured packages…</div></div>
            <p class="lr-featured-summary" id="featured-summary"></p>
          </div>
        </section>

        <section class="lr-start">
          <div class="lr-start-inner">
            <div class="lr-start-copy">
              <span class="lr-section-index">05 / YOUR TURN</span>
              <h2>Have an idea?<br><em>Give it a file.</em></h2>
              <p>Install the launcher, open the beginner guide, and run your first program. LUC runs on Windows and Linux.</p>
              <div class="lr-start-actions">
                <a class="lr-button lr-button-orange" href="#/download">${icon('download', { size: 17 })} Get LUC ${icon('arrow-right', { size: 16 })}</a>
                <a class="lr-start-guide" href="#/guide">Beginner guide ${icon('arrow-right', { size: 15 })}</a>
              </div>
            </div>
            <div class="lr-terminal-card">
              <div class="lr-terminal-head"><span>${icon('terminal', { size: 15 })} QUICK START</span><span>01—02</span></div>
              <div class="lr-terminal-command"><i>01</i><span class="lr-prompt">$</span> npm install -g @hsusulist/luc</div>
              <div class="lr-terminal-note">Install the launcher and runtime.</div>
              <div class="lr-terminal-command"><i>02</i><span class="lr-prompt">$</span> luc hello.luc</div>
              <div class="lr-terminal-note">Run a LUC program.</div>
              <div class="lr-terminal-status"><span><i></i> READY</span><span>WINDOWS / LINUX</span></div>
            </div>
          </div>
          <div class="lr-start-edge" aria-hidden="true">WRITE IT. RUN IT. KEEP GOING.</div>
        </section>
      </div>
    `,
    async after() {
      observeReveal();
      const wrap = document.getElementById('featured-packages');
      const summary = document.getElementById('featured-summary');
      if (!wrap) return;
      try {
        const res = await fetch('/api/packages?sort=downloads');
        if (!res.ok) throw new Error('registry unavailable');
        const data = await res.json();
        const featured = data.packages.filter((pkg) => pkg.featured).slice(0, 3);
        const packages = featured.length ? featured : data.packages.slice(0, 3);
        wrap.innerHTML = packages.length
          ? packages.map(packageCard).join('')
          : '<p class="lr-featured-empty">No packages are published yet. Be the first to share one.</p>';
        if (summary) {
          summary.textContent = `${formatNumber(data.stats.packages)} packages · ${formatNumber(data.stats.weeklyDownloads, true)} installs a week`;
        }
      } catch {
        wrap.innerHTML = `
          <div class="lr-featured-empty">
            <strong>The registry is taking a nap.</strong>
            <span>We could not reach the package API. You can still browse the source.</span>
            <a href="https://github.com/hsusulist/luc" target="_blank" rel="noopener noreferrer">Open GitHub ${icon('arrow-up-right', { size: 14 })}</a>
          </div>`;
      }
    }
  };
}
