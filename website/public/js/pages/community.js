import { icon } from '../lib/icons.js';
import { highlight } from '../lib/highlight.js';
import { codeBlock, commandBox, pageHero, sectionHead, observeReveal } from '../lib/ui.js';

const showcases = [
  {
    name: 'pong.luc',
    tag: 'window',
    desc: 'A full two-player Pong in about 200 lines — input, sound and all.',
    code: `import window("w")

w.go("Pong", 800, 500, function(dt)
    update(dt)
    w.clear("black")
    draw_paddles()
    draw_ball()
end)`
  },
  {
    name: 'bot.luc',
    tag: 'discord',
    desc: 'A prefix and slash command bot with embeds and buttons.',
    code: `import discord("bot")

bot:run(bot.token(os.getenv("TOKEN")))
while bot.run do
    create cmd = bot.prefix("!")
    if cmd == "ping" then
        bot.msg:send("pong!")
    end
end`
  },
  {
    name: 'train.luc',
    tag: 'ai',
    desc: 'Fit a tiny network on CPU, then generate text from a language model.',
    code: `import ai

create model = ai.LMTrain {
    data = "hello world, this is luc ai!",
    preset = "auto",
    epochs = 300
}
model:run()
print(model:generate("hello", 20))`
  },
  {
    name: 'chat.luc',
    tag: 'net',
    desc: 'A multi-client TCP chat server with non-blocking tasks.',
    code: `import net("n")

n.serve(8080, function(client)
    create msg = client:read()
    n.broadcast(msg, client)
    return 200, "ok"
end)`
  }
];

export function renderCommunity() {
  return {
    title: 'Community — build LUC with us',
    description: 'Join the LUC community: source code, discussions, issues, showcases, contribution guides and the roadmap.',
    html: `
      ${pageHero({
        eyebrow: 'Community',
        title: 'Built in public.<br /><span class="grad">Better with you.</span>',
        lede: 'LUC is a small language with an open process. The source, the roadmap and the arguments about syntax all live in the same place — and there is room for another voice.',
        actions: `
          <a class="btn btn-primary" href="https://github.com/hsusulist/luc" target="_blank" rel="noopener noreferrer">${icon('github', { size: 16 })} Star on GitHub</a>
          <a class="btn btn-ghost" href="https://github.com/hsusulist/luc/discussions" target="_blank" rel="noopener noreferrer">${icon('message', { size: 16 })} Open discussions</a>`,
        meta: `
          <span>${icon('users', { size: 14 })} Contributors welcome</span>
          <span>${icon('scale', { size: 14 })} Apache 2.0</span>
          <span>${icon('heart', { size: 14 })} Be kind, be specific</span>`
      })}

      <section class="section-tight">
        <div class="container">
          <div class="community-grid">
            <a class="community-card reveal" href="https://github.com/hsusulist/luc" target="_blank" rel="noopener noreferrer">
              <div class="community-card-head">${icon('github', { size: 19 })}<h3>GitHub repository</h3></div>
              <p>The interpreter, LCode, the VS Code grammar, demos and the AI modules. Issues, pull requests and releases all live here.</p>
              <span class="link-arrow">github.com/hsusulist/luc ${icon('arrow-up-right', { size: 14 })}</span>
            </a>

            <a class="community-card reveal" style="--d:60ms" href="https://github.com/hsusulist/luc/discussions" target="_blank" rel="noopener noreferrer">
              <div class="community-card-head">${icon('message', { size: 19 })}<h3>Discussions</h3></div>
              <p>Show what you built, ask a question that is not a bug, propose a feature, or argue about whether commands should return values.</p>
              <span class="link-arrow">Join the conversation ${icon('arrow-up-right', { size: 14 })}</span>
            </a>

            <a class="community-card reveal" style="--d:120ms" href="https://github.com/hsusulist/luc/issues" target="_blank" rel="noopener noreferrer">
              <div class="community-card-head">${icon('bug', { size: 19 })}<h3>Issue tracker</h3></div>
              <p>Found a crash, a wrong error message or a platform quirk? A short repro and the version output is enough to get it moving.</p>
              <span class="link-arrow">Report a bug ${icon('arrow-up-right', { size: 14 })}</span>
            </a>

            <div class="community-card is-muted reveal" style="--d:180ms">
              <div class="community-card-head">${icon('users', { size: 19 })}<h3>Discord</h3><span class="badge badge-beta" style="margin-left:auto">soon</span></div>
              <p>A real-time room is being set up. Until the invite lands, Discussions is the place — it is threaded, searchable and doesn't vanish.</p>
              <span class="link-arrow" style="color:var(--muted-2)">Invite coming soon</span>
            </div>
          </div>

          <div class="credits-band reveal" style="margin-top:20px">
            <div style="display:flex;align-items:center;gap:16px;flex-wrap:wrap">
              <div class="avatar-stack" aria-hidden="true"><span>hs</span><span>mi</span><span>li</span><span>ph</span></div>
              <p><strong>Every package here started as someone's first .luic.</strong><br />Publish yours and add a line to the registry.</p>
            </div>
            <a class="btn btn-soft btn-sm" href="#/publish">${icon('upload', { size: 15 })} Publish a package</a>
          </div>
        </div>
      </section>

      <section class="section">
        <div class="container">
          ${sectionHead({
            eyebrow: 'Showcase',
            title: 'Small programs, actual output.',
            lede: 'These are stripped-down versions of demos in the repository. Each one runs with a single import.'
          })}

          <div class="showcase-grid">
            ${showcases.map((item, index) => `
              <article class="showcase-card reveal" style="--d:${index * 60}ms">
                <div class="showcase-card-head">
                  <div>
                    <h3>${item.name}</h3>
                    <p>${item.desc}</p>
                  </div>
                  <span class="showcase-tag">${item.tag}</span>
                </div>
                <div class="mini-code" style="white-space:pre-wrap">${highlight(item.code)}</div>
              </article>`).join('')}
          </div>

          <div class="section-actions">
            <a class="btn btn-ghost" href="https://github.com/hsusulist/luc/tree/main/demos" target="_blank" rel="noopener noreferrer">${icon('code', { size: 15 })} Browse all demos ${icon('arrow-up-right', { size: 14 })}</a>
          </div>
        </div>
      </section>

      <section class="section">
        <div class="container">
          ${sectionHead({
            eyebrow: 'Contributing',
            title: 'Three ways in, none of them scary.',
            lede: 'You do not need to write C to help. Docs, examples, packages, bug reports and grammar fixes all move the project forward.'
          })}

          <div class="contribute-steps">
            <article class="contribute-step reveal">
              <h3>Use it, then say what broke</h3>
              <p>Run a demo, port a small script, try the installer on a clean machine. File issues with the version, platform and the exact line that failed.</p>
            </article>
            <article class="contribute-step reveal" style="--d:70ms">
              <h3>Write something shareable</h3>
              <p>Turn a helper into a library part, write a README, publish it here. The registry is designed so one person can ship a package in a few minutes.</p>
            </article>
            <article class="contribute-step reveal" style="--d:140ms">
              <h3>Send a focused pull request</h3>
              <p>One change, one reason, with a test or a repro. For the interpreter, build it with the command below and mention the platform you tested on.</p>
            </article>
          </div>

          <div class="split" style="margin-top:24px">
            <div>
              <h3 style="font-size:16px;margin-bottom:10px">Build and test locally</h3>
              <p style="font-size:13.5px;color:var(--muted);line-height:1.7">
                Clone the repository, build the interpreter with a C99 compiler, then run the demos from the repo root.
                The Windows build also accepts the SDL2 window library through MinGW.
              </p>
              <div class="callout callout-tip">${icon('sparkles', { size: 16 })}<div><strong>First contribution idea:</strong> take a demo in <code>demos/</code>, turn it into a documented package, and publish it.</div></div>
            </div>
            <div class="reveal" style="--d:80ms">
              ${codeBlock(`git clone https://github.com/hsusulist/luc.git
cd luc

# Linux / macOS-style build
cc -O2 -std=gnu99 -o luc \\
  src/luc_core.c src/luc_libs.c src/luc_trans.c \\
  -lm -ldl

./luc demos/net_libaries/http.luc`, { filename: 'terminal', lang: 'bash', lineNumbers: true })}
              ${commandBox('git checkout -b feature/my-first-fix', { label: 'Then branch and make it' })}
            </div>
          </div>
        </div>
      </section>

      <section class="section">
        <div class="container">
          ${sectionHead({
            eyebrow: 'Roadmap',
            title: 'What is done, and what is next.',
            lede: 'Priorities change with feedback. If something here matters to you, say so in Discussions — that is how items move up.'
          })}

          <div class="split">
            <div class="roadmap reveal">
              <div class="roadmap-item done">
                <h4>Interpreter &amp; VM <span class="roadmap-state">shipped</span></h4>
                <p>Bytecode VM, inline caches, coroutines, tasks, metatables and the C99 core.</p>
              </div>
              <div class="roadmap-item done">
                <h4>Batteries <span class="roadmap-state">shipped</span></h4>
                <p>window, net, json, buffer, the lanternl AI port and the Discord bot library.</p>
              </div>
              <div class="roadmap-item done">
                <h4>Package registry <span class="roadmap-state">shipped</span></h4>
                <p>Publish, search, version history, readmes and one-command install from this page.</p>
              </div>
              <div class="roadmap-item now">
                <h4>Docs &amp; guide <span class="roadmap-state now">in progress</span></h4>
                <p>More worked examples, a complete library reference and a printable cheat sheet.</p>
              </div>
              <div class="roadmap-item now">
                <h4>Linux parity <span class="roadmap-state now">in progress</span></h4>
                <p>Closer parity for the terminal editor and build tooling across distributions.</p>
              </div>
              <div class="roadmap-item">
                <h4>A web playground</h4>
                <p>Run a snippet in the browser without installing anything. Same bytecode, compiled to WebAssembly.</p>
              </div>
              <div class="roadmap-item">
                <h4>GPU bridge for AI</h4>
                <p>Load native GPU backends instead of the currently stubbed path, for larger training runs.</p>
              </div>
            </div>

            <div class="reveal" style="--d:80ms">
              <div class="side-card" style="padding:24px">
                <h3>House rules</h3>
                <div class="feature-list" style="margin-top:0">
                  <li>${icon('check', { size: 14 })} <span>Assume good faith. Ask before assuming a mistake.</span></li>
                  <li>${icon('check', { size: 14 })} <span>Critique the code, not the person who wrote it.</span></li>
                  <li>${icon('check', { size: 14 })} <span>Small repros beat long essays. Every time.</span></li>
                  <li>${icon('check', { size: 14 })} <span>Beginners are explicitly welcome to ask basic questions.</span></li>
                </div>
              </div>
              <div class="side-card" style="padding:24px">
                <h3>Good first issues</h3>
                <p style="font-size:12.5px;line-height:1.65;color:var(--muted)">
                  Look for issues tagged <code style="font-family:var(--font-mono);font-size:10.5px;color:var(--accent-2)">good first issue</code> on GitHub.
                  Documentation, example files and error-message improvements are often the easiest entry points.
                </p>
                <a class="btn btn-ghost btn-sm" style="margin-top:14px" href="https://github.com/hsusulist/luc/issues" target="_blank" rel="noopener noreferrer">${icon('github', { size: 14 })} Find an issue</a>
              </div>
            </div>
          </div>
        </div>
      </section>

      <section class="section section-tight">
        <div class="container">
          <div class="cta-band reveal">
            <div>
              <p class="eyebrow" style="margin-bottom:10px"><span class="eyebrow-dot"></span> Registry</p>
              <h2>Your library belongs here.</h2>
              <p>Name it, describe it, drop in a README if you have one. Publish a version and the install command is ready to copy — no build pipeline, no waiting.</p>
              <div class="cta-band-actions">
                <a class="btn btn-primary" href="#/publish">${icon('upload', { size: 16 })} Publish a package</a>
                <a class="btn btn-ghost" href="#/packages">${icon('package', { size: 16 })} Browse the registry</a>
              </div>
            </div>
            <div class="cta-band-code">
              ${commandBox('luc pkg add mymath', { label: 'Once published', note: 'Or copy the exact command from your package page.' })}
            </div>
          </div>
        </div>
      </section>
    `,
    after() {
      observeReveal();
    }
  };
}
