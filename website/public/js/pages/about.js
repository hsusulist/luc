import { icon } from '../lib/icons.js';
import { codeBlock, sectionHead, observeReveal } from '../lib/ui.js';

const anatomyCode = `!strict
import libs

make mathx

command banner(title)
    print("== " + title + " ==")
end

create function add(a, b)
    return a + b
end

create numbers = [3, 5, 8, 13]
create total = 0

repeat n, i in numbers do
    total += n
end

run banner("fibonacci")
print("total: " + tostring(total))`;

export function renderAbout() {
  return {
    title: 'About LUC — a small language with big batteries',
    description: 'The design, the syntax and the trade-offs behind LUC: a C99 scripting language that stays readable from the first line to the last.',
    html: `
      <section class="about-hero">
        <div class="container">
          <p class="eyebrow reveal"><span class="eyebrow-dot"></span> About the language</p>
          <h1 class="reveal" style="--d:60ms">Small enough to read.<br /><em>Sharp enough to ship.</em></h1>
          <p class="page-lede reveal" style="--d:120ms">
            LUC started with a simple annoyance: the languages that are pleasant to write are often
            missing the pieces you need, and the languages that have the pieces are rarely pleasant.
            LUC is an attempt to stop choosing.
          </p>
          <div class="page-meta reveal" style="--d:180ms">
            <span>${icon('zap', { size: 14 })} First release 2026</span>
            <span>${icon('user', { size: 14 })} Created by hsusulist</span>
            <span>${icon('scale', { size: 14 })} Apache 2.0</span>
            <span>${icon('cpu', { size: 14 })} C99 · bytecode VM</span>
          </div>
        </div>
      </section>

      <section class="section section-tight">
        <div class="container">
          <div class="split">
            <div>
              ${sectionHead({
                eyebrow: 'The idea',
                title: 'Easy should be a property of the language, not a phase.',
                lede: 'Most languages make the first hour easy and the second month painful. LUC tries to keep the same shape all the way down.'
              })}
              <p style="color:var(--muted);font-size:14.5px;line-height:1.75">
                Values are few: numbers, strings, booleans, lists, dicts, functions and buffers.
                Keywords are fewer still. There is one obvious way to declare a variable, one way
                to write a procedure, and one way to loop over a collection. The grammar is regular
                enough that you can predict a line you have never seen.
              </p>
              <p style="color:var(--muted);font-size:14.5px;line-height:1.75;margin-top:12px">
                Then the batteries: windows, HTTP, sockets, JSON, AI, Discord. They are ordinary
                imports, not a separate platform. A script you write on a laptop runs on a server
                with the same interpreter, and a library you publish is a file you can read.
              </p>
            </div>
            <div class="reveal" style="--d:80ms">
              ${codeBlock(`-- the entire "declare" story
create name = "luc"
create version = 0.2
create stable = false
create tags = ["small", "fast"]

-- explicit, not implied
create function shout(text)
    return string.upper(text) + "!"
end

print(shout(name))   -- LUC!`, { filename: 'values.luc', lineNumbers: true })}
            </div>
          </div>
        </div>
      </section>

      <section class="section">
        <div class="container">
          ${sectionHead({
            eyebrow: 'Design rules',
            title: 'Three rules we keep coming back to.',
            lede: 'When a feature is proposed, it has to survive these. Most do not, which is the point.'
          })}

          <div class="principle-grid">
            <article class="principle reveal">
              <span class="principle-num">01 / READ</span>
              <h3>If you can say it, you can read it.</h3>
              <p>Keywords are English words with one meaning each. <code>create</code> makes something. <code>command</code> does something. <code>repeat</code> repeats. No symbol soup, no operators standing in for three different ideas.</p>
            </article>
            <article class="principle reveal" style="--d:70ms">
              <span class="principle-num">02 / SMALL</span>
              <h3>One binary, zero runtime.</h3>
              <p>The interpreter is C99 with no runtime dependencies. The npm package is a launcher, not a second ecosystem. If a feature can live in a library, it lives in a library.</p>
            </article>
            <article class="principle reveal" style="--d:140ms">
              <span class="principle-num">03 / OPEN</span>
              <h3>Source you can hold.</h3>
              <p>Libraries ship as readable <code>.luic</code> files, packages are source-first, and the whole interpreter is Apache 2.0. Nothing important hides behind a build server.</p>
            </article>
          </div>
        </div>
      </section>

      <section class="section">
        <div class="container">
          ${sectionHead({
            eyebrow: 'Anatomy',
            title: 'One file, every idea.',
            lede: 'This is a complete LUC program. Every annotation on the right points at a decision made on purpose.'
          })}

          <div class="anatomy">
            <div class="reveal">
              ${codeBlock(anatomyCode, { filename: 'fibonacci.luc', lineNumbers: true, badge: 'complete program' })}
            </div>
            <div class="anatomy-notes reveal" style="--d:80ms">
              <div class="anatomy-note">
                <span class="anatomy-note-num">1</span>
                <div>
                  <h4><code>!strict</code> is opt-in</h4>
                  <p>Turn it on and the compiler requires <code>create</code>, quoted dict keys and explicit <code>run</code> calls. Leave it off for scripts and prototypes.</p>
                </div>
              </div>
              <div class="anatomy-note">
                <span class="anatomy-note-num">2</span>
                <div>
                  <h4><code>make</code> turns a file into a library part</h4>
                  <p>Everything top-level that you <code>create</code> is exported automatically. No export list to forget to update.</p>
                </div>
              </div>
              <div class="anatomy-note">
                <span class="anatomy-note-num">3</span>
                <div>
                  <h4>Two kinds of callable</h4>
                  <p><code>create function</code> returns a value. <code>command</code> is a procedure and cannot return. The distinction is enforced at compile time.</p>
                </div>
              </div>
              <div class="anatomy-note">
                <span class="anatomy-note-num">4</span>
                <div>
                  <h4>One loop for everything</h4>
                  <p><code>repeat 5 do</code>, <code>repeat 1, 10 as i do</code> and <code>repeat item, i in list do</code> cover counts and collections without a second syntax.</p>
                </div>
              </div>
              <div class="anatomy-note">
                <span class="anatomy-note-num">5</span>
                <div>
                  <h4>Compound assignment</h4>
                  <p><code>+=</code>, <code>-=</code>, <code>*=</code> and <code>/=</code> work on numbers and keep long loops readable.</p>
                </div>
              </div>
            </div>
          </div>
        </div>
      </section>

      <section class="section">
        <div class="container">
          ${sectionHead({
            eyebrow: 'Under the hood',
            title: 'A conventional engine, hiding behind plain words.',
            lede: 'LUC compiles to bytecode and runs on a register VM. Nothing exotic — just careful, fast, and small enough to debug at 2am.'
          })}

          <div class="arch reveal">
            <div class="arch-node">
              <span>Step 01</span>
              <strong>Lexer</strong>
              <p>Words, strings, numbers. <code>local</code> gets a helpful error.</p>
            </div>
            <div class="arch-arrow">${icon('chevron-right', { size: 18 })}</div>
            <div class="arch-node">
              <span>Step 02</span>
              <strong>Compiler</strong>
              <p>Single-pass parse to register bytecode with line info.</p>
            </div>
            <div class="arch-arrow">${icon('chevron-right', { size: 18 })}</div>
            <div class="arch-node">
              <span>Step 03</span>
              <strong>Bytecode VM</strong>
              <p>Inline caches, mark-and-sweep GC, coroutines, tasks.</p>
            </div>
            <div class="arch-arrow">${icon('chevron-right', { size: 18 })}</div>
            <div class="arch-node">
              <span>Step 04</span>
              <strong>JIT &amp; AOT</strong>
              <p>Numeric hot loops run as native code on Windows x64.</p>
            </div>
            <div class="arch-arrow">${icon('chevron-right', { size: 18 })}</div>
            <div class="arch-node">
              <span>Step 05</span>
              <strong>Native libs</strong>
              <p>window, net, buffers and the AI stack call straight out.</p>
            </div>
          </div>

          <div class="bento" style="margin-top:18px">
            <article class="bento-card span-4 reveal" data-spotlight>
              <span class="card-icon">${icon('shield', { size: 20 })}</span>
              <h3>Strict mode, on demand</h3>
              <p><code>!strict</code> turns implicit globals, bare procedures and unquoted dict keys into compile errors. Ideal for libraries other people will use.</p>
            </article>
            <article class="bento-card span-4 reveal" style="--d:70ms" data-spotlight>
              <span class="card-icon violet">${icon('layers', { size: 20 })}</span>
              <h3>Exact big integers</h3>
              <p><code>!karatsuba</code> makes integer-string multiplication exact. Useful for cryptography exercises, boring for everyone else — which is why it is a flag.</p>
            </article>
            <article class="bento-card span-4 reveal" style="--d:140ms" data-spotlight>
              <span class="card-icon green">${icon('command', { size: 20 })}</span>
              <h3>Metatables &amp; classes</h3>
              <p><code>setmetatable</code>, <code>__index</code>, <code>__call</code> and friends work out of the box, so object patterns are a library concern, not a language feature.</p>
            </article>
          </div>
        </div>
      </section>

      <section class="section">
        <div class="container">
          ${sectionHead({
            eyebrow: 'Honest comparison',
            title: 'LUC next to Lua.',
            lede: 'LUC borrowed Lua\'s values and some VM ideas. It did not borrow its grammar — and it ships more by default.'
          })}

          <div class="reveal" style="overflow-x:auto">
            <table class="compare-table">
              <thead>
                <tr><th>Thing</th><th>Lua</th><th>LUC</th></tr>
              </thead>
              <tbody>
                <tr><td>Local declaration</td><td><code>local x = 1</code></td><td><code>create x = 1</code></td></tr>
                <tr><td>Procedures</td><td>Function returning nothing</td><td><code>command</code>, enforced by the compiler</td></tr>
                <tr><td>Counting loop</td><td><code>for i = 1, 10 do</code></td><td><code>repeat 1, 10 as i do</code></td></tr>
                <tr><td>Library format</td><td>External rocks + paths</td><td><code>make</code> / <code>get</code> / <code>pack</code> built in</td></tr>
                <tr><td>Package registry</td><td>LuaRocks</td><td>Right here on this site</td></tr>
                <tr><td>Windows &amp; sound</td><td>Third-party binding</td><td><code>import window</code></td></tr>
                <tr><td>HTTP &amp; sockets</td><td>Third-party binding</td><td><code>import net</code></td></tr>
                <tr><td>AI / tensors</td><td>External tooling</td><td><code>import ai</code></td></tr>
                <tr><td>Strict mode</td><td>Optional analyzers</td><td><code>!strict</code> in the file</td></tr>
                <tr><td>License</td><td>MIT</td><td>Apache 2.0</td></tr>
              </tbody>
            </table>
          </div>
        </div>
      </section>

      <section class="section">
        <div class="container container-narrow">
          ${sectionHead({
            eyebrow: 'Questions',
            title: 'The things people ask first.',
            align: 'center'
          })}
          <div class="faq reveal">
            <details>
              <summary>Is LUC just Lua with new keywords? ${icon('chevron-down', { size: 16 })}</summary>
              <div class="faq-body">No. LUC shares Lua's value model and some VM ideas, but the grammar is its own: <code>local</code> is rejected, declarations use <code>create</code>, procedures are a separate construct, and user libraries are part of the core. Familiar looking expressions are a feature, not a port.</div>
            </details>
            <details>
              <summary>Can I run my existing Lua code? ${icon('chevron-down', { size: 16 })}</summary>
              <div class="faq-body">Not as-is. Many expressions look the same, but blocks, declarations and library loading all use LUC syntax. Porting small scripts is usually mechanical; large codebases are a rewrite.</div>
            </details>
            <details>
              <summary>Does it work on Linux? ${icon('chevron-down', { size: 16 })}</summary>
              <div class="faq-body">Yes. The core interpreter and terminal LCode editor build on Linux with GCC or Clang, and the npm package does that automatically. The Windows build adds the SDL2 window library and the Windows x64 JIT.</div>
            </details>
            <details>
              <summary>How fast is it, really? ${icon('chevron-down', { size: 16 })}</summary>
              <div class="faq-body">The interpreter is a register bytecode VM written in C99 with inline caches for table access. Numeric hot loops can be JIT compiled on Windows x64. Startup is around two milliseconds — LUC does not load a runtime before your first line.</div>
            </details>
            <details>
              <summary>Can I use it commercially? ${icon('chevron-down', { size: 16 })}</summary>
              <div class="faq-body">Yes. LUC is released under the Apache 2.0 license, which permits commercial use, modification and redistribution with the usual notice and patent terms. Check the license for the details.</div>
            </details>
            <details>
              <summary>Where do packages come from? ${icon('chevron-down', { size: 16 })}</summary>
              <div class="faq-body">The registry on this site. Packages are published from the website, and the install command you copy pulls the package metadata from the same page. The format is deliberately simple: a name, a readme, a version, a license and a source repository.</div>
            </details>
          </div>

          <div class="credits-band reveal" style="margin-top:24px">
            <p><strong>Made by Duy (hsusulist)</strong> · 2026 · Apache 2.0<br />Built with a C compiler, a lot of coffee and a stubborn belief that small can be sharp.</p>
            <a class="btn btn-ghost btn-sm" href="https://github.com/hsusulist/luc" target="_blank" rel="noopener noreferrer">${icon('github', { size: 15 })} See the source</a>
          </div>
        </div>
      </section>
    `,
    after() {
      observeReveal();
    }
  };
}
