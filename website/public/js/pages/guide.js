import { icon } from '../lib/icons.js';
import { codeBlock, commandBox, observeReveal, pageHero } from '../lib/ui.js';

const chapters = [
  { id: 'install', label: 'Install & run' },
  { id: 'hello', label: 'Hello, world' },
  { id: 'variables', label: 'Variables' },
  { id: 'values', label: 'Values & types' },
  { id: 'operators', label: 'Operators & strings' },
  { id: 'collections', label: 'Lists & dicts' },
  { id: 'conditions', label: 'Conditions' },
  { id: 'loops', label: 'Loops' },
  { id: 'functions', label: 'Functions' },
  { id: 'commands', label: 'Commands' },
  { id: 'imports', label: 'Imports' },
  { id: 'libraries', label: 'Libraries' },
  { id: 'errors', label: 'Errors' },
  { id: 'strict', label: 'Strict mode' },
  { id: 'next', label: 'Next steps' }
];

function chapter(number, id, title, body) {
  return `
    <section class="chapter" id="${id}">
      <span class="chapter-kicker">Chapter ${String(number).padStart(2, '0')}</span>
      <h2>${title}</h2>
      ${body}
    </section>`;
}

export function renderGuide() {
  const toc = chapters
    .map((c, index) => `<a href="#/guide#${c.id}" data-scroll="${c.id}"><span>${String(index + 1).padStart(2, '0')}</span>${c.label}</a>`)
    .join('');

  return {
    title: 'Beginner guide — learn LUC in an afternoon',
    description: 'A hands-on introduction to LUC: install it, write your first program, and learn variables, loops, functions, commands, libraries and packages.',
    html: `
      <div class="guide-progress" id="guide-progress"></div>

      ${pageHero({
        eyebrow: 'Beginner guide',
        title: 'From zero to a working script<br /><span class="grad">in one sitting.</span>',
        lede: 'Fifteen short chapters, no assumed knowledge. Every snippet runs as written — copy it, change it, break it. That is how the language sticks.',
        meta: `
          <span>${icon('clock', { size: 14 })} About 45 minutes</span>
          <span>${icon('code', { size: 14 })} Uses v0.2.0-beta-1</span>
          <span>${icon('check', { size: 14 })} No prior Lua needed</span>`
      })}

      <div class="container">
        <div class="guide-layout">
          <aside class="guide-sidebar">
            <p class="guide-sidebar-title">On this page</p>
            <nav class="guide-toc" id="guide-toc" aria-label="Guide chapters">${toc}</nav>
            <div class="guide-sidebar-foot">
              <strong>Stuck?</strong>
              Open an issue on GitHub or ask in the community. Include the line that failed and the output — it is usually enough.
              <div style="margin-top:10px"><a class="link-arrow" href="#/community">Community ${icon('arrow-right', { size: 13 })}</a></div>
            </div>
          </aside>

          <div class="guide-content">
            ${chapter(1, 'install', 'Install LUC and open the REPL', `
              <p>LUC runs on Windows and Linux. The fastest route on either platform is the npm launcher; Windows also has a signed installer.</p>
              ${commandBox('npm install -g @hsusulist/luc', { label: 'Windows or Linux', note: 'On Linux, this compiles the runtime during install. You need a C compiler such as GCC or Clang.' })}
              <p>Windows users who prefer a GUI can grab the <a href="#/download">installer</a> instead. Once it is installed, confirm the version:</p>
              ${codeBlock(`luc --version
# LUC 0.2.0-beta-1`, { filename: 'terminal', lang: 'bash' })}
              <p>Run <code>luc</code> with no arguments to open the interactive prompt. Type an expression and press Enter — this is the REPL, and it is the fastest way to test an idea.</p>
              ${codeBlock(`$ luc
|  ❯ 1 + 1
2
|  ❯ "luc" + "!"
luc!`, { filename: 'REPL', lang: 'bash' })}
              <div class="callout callout-tip">${icon('sparkles', { size: 16 })}<div><strong>Editor included.</strong> Run <code>lcode</code> in a project folder for the bundled editor, or install the VS Code extension from the repository.</div></div>
            `)}

            ${chapter(2, 'hello', 'Hello, world', `
              <p>Create a file called <code>hello.luc</code>. The extension is <code>.luc</code>. Write one line:</p>
              ${codeBlock(`print("Hello, LUC!")`, { filename: 'hello.luc', lineNumbers: true })}
              <p>Run it from the same folder:</p>
              ${commandBox('luc hello.luc', { note: 'Output: Hello, LUC!' })}
              <p><code>print</code> is a built-in function. It writes each argument to the terminal, separated by tabs, and adds a newline at the end. For output without a newline, use <code>io.write</code>.</p>
              ${codeBlock(`print("one", 2, true)   -- one    2    true
io.write("no newline")`, { filename: 'hello.luc', lineNumbers: true })}
              <p>For a one-off script you can skip the file entirely:</p>
              ${commandBox('luc -e "print(\\"hi from the command line\\")"', { label: 'Inline' })}
              <div class="callout">${icon('info', { size: 16 })}<div>Comments start with <code>--</code> and run to the end of the line. Block comments use <code>--[[ ... ]]</code>.</div></div>
            `)}

            ${chapter(3, 'variables', 'Variables, with and without create', `
              <p>Use <code>create</code> to declare a variable. You can create several at once and destructure a list on the right-hand side.</p>
              ${codeBlock(`create name = "LUC"
create version = 0.2
create is_fast = true

create x, y = 10, 20
print(x + y)          -- 30

create first, second = 3, 4
print(second)         -- 4`, { filename: 'variables.luc', lineNumbers: true })}
              <p>Assignment changes an existing variable. If you assign to a name that was never created, LUC makes it a <em>global</em> — useful for quick scripts, forbidden in strict mode.</p>
              ${codeBlock(`create count = 0
count = count + 1
count += 1             -- same thing, shorter
print(count)           -- 2

total = 100            -- global (needs create under !strict)`, { filename: 'variables.luc', lineNumbers: true })}
              <h3>Valid names</h3>
              <p>Names may contain letters, digits and underscores, and cannot start with a digit. Keywords such as <code>create</code> and <code>end</code> are reserved. <code>local</code> is rejected with a hint, because LUC is not Lua.</p>
              <div class="callout callout-warn">${icon('alert', { size: 16 })}<div>Add <code>!strict</code> as the first line of a file to require <code>create</code>. It is a good habit for anything you plan to share.</div></div>
            `)}

            ${chapter(4, 'values', 'Values, types and truthiness', `
              <p>There are eight values you will meet on day one. <code>type</code> tells you which one you have.</p>
              ${codeBlock(`create values = [
    42,                    -- number
    "hello",               -- string
    true,                  -- boolean
    nil,                   -- nothing
    [1, 2, 3],             -- list
    {"name": "luc"},       -- dict
    function(a) return a end
]

repeat value, i in values do
    print(i + ": " + type(value))
end`, { filename: 'types.luc', lineNumbers: true })}
              <p>Only <code>false</code> and <code>nil</code> are falsy. Zero and the empty string are truthy, which surprises people coming from JavaScript or Python.</p>
              ${codeBlock(`if 0 then print("zero is truthy") end
if "" then print("empty strings are truthy too") end`, { filename: 'truthiness.luc' })}
              <h3>Numbers</h3>
              <p>All numbers are doubles. Integer operations stay exact within the usual 53-bit range; for arbitrary precision integer <em>strings</em>, use <code>!karatsuba</code> and <code>math.karatsuba</code>.</p>
              ${codeBlock(`print(math.floor(3.9))   -- 3
print(10 / 3)             -- 3.3333333333333
print(math.floor(10 / 3)) -- 3
print(math.tointeger(10 / 2))`, { filename: 'numbers.luc', lineNumbers: true })}
              <div class="callout callout-warn">${icon('alert', { size: 16 })}<div>There is no <code>//</code> operator in LUC. Use <code>math.floor(10 / 3)</code>.</div></div>
            `)}

            ${chapter(5, 'operators', 'Operators and strings', `
              <p>The arithmetic, comparison and boolean operators are the ones you expect. Concatenation uses <code>+</code>, not <code>..</code>.</p>
              ${codeBlock(`print(2 + 3 * 4)         -- 14
print(10 % 3)            -- 1
print(2 ^ 10)            -- 1024
print("luc" + " " + "rocks")  -- luc rocks

print(3 == 3, 3 != 4, 3 < 4, 4 >= 4)
print(true and false, true or false, not false)`, { filename: 'operators.luc', lineNumbers: true })}
              <p>Strings are immutable. The <code>string</code> table has the tools that other languages put in a stdlib.</p>
              ${codeBlock(`create s = "  Fast, small, friendly  "

print(string.trim(s))            -- Fast, small, friendly
print(string.upper(s))           --   FAST, SMALL, FRIENDLY
print(string.len(s))             -- 25
print(len(s))                    -- 25 (len works on strings too)

print(string.split("a,b,c", ","))    -- list: a b c
print(string.startswith("luc", "lu")) -- true
print(string.contains(s, "small"))    -- true

print(string.sub("abcdef", 2, 4))     -- bcd (1-based)
print(string.rep("ab", 3))            -- ababab`, { filename: 'strings.luc', lineNumbers: true })}
              <p>Use <code>string.format</code> when concatenation gets noisy:</p>
              ${codeBlock(`create name, score = "an", 42
print(string.format("%s scored %d points", name, score))`, { filename: 'format.luc' })}
            `)}

            ${chapter(6, 'collections', 'Lists and dictionaries', `
              <p>Lists use square brackets and are 1-based. Dicts use braces with string keys, accessed with a dot or brackets.</p>
              ${codeBlock(`create fruits = ["apple", "banana", "cherry"]

print(fruits[1])          -- apple
print(len(fruits))        -- 3

fruits.append("date")
fruits.insert(2, "apricot")
fruits.remove(1)

repeat fruit, i in fruits do
    print(i + ". " + fruit)
end`, { filename: 'lists.luc', lineNumbers: true })}
              <p>List methods cover the everyday cases: <code>append</code>, <code>pop</code>, <code>insert</code>, <code>remove</code>, <code>contains</code>, <code>indexof</code>, <code>sort</code>, <code>reverse</code>, <code>extend</code> and <code>clear</code>.</p>
              ${codeBlock(`create scores = {"an": 12, "linh": 18}
scores["minh"] = 15

print(scores.an)           -- 12
print(scores["linh"])      -- 18
print(len(scores))         -- 3

create missing = scores.nobody
print(missing)             -- nil`, { filename: 'dicts.luc', lineNumbers: true })}
              <div class="callout callout-tip">${icon('sparkles', { size: 16 })}<div><strong>Mix them.</strong> Lists of dicts are the natural shape for records — exactly what the AI library expects for training data.</div></div>
              ${codeBlock(`create users = [
    {"name": "an", "score": 12},
    {"name": "linh", "score": 18},
]

repeat user, i in users do
    print(user.name + ": " + tostring(user.score))
end`, { filename: 'records.luc', lineNumbers: true })}
            `)}

            ${chapter(7, 'conditions', 'Conditions and branches', `
              <p><code>if</code> blocks end with <code>end</code>. Use <code>elseif</code> and <code>else</code> for more branches; parentheses around the condition are optional.</p>
              ${codeBlock(`create score = 86

if score >= 90 then
    print("A")
elseif score >= 80 then
    print("B")
elseif score >= 70 then
    print("C")
else
    print("keep going")
end`, { filename: 'grades.luc', lineNumbers: true })}
              <p>Single-line branches are allowed when the body is short — handy for guards:</p>
              ${codeBlock(`create x = 12
if x > 10 then print("big") end
print(x > 10 and "big" or "small")   -- expression form`, { filename: 'guards.luc', lineNumbers: true })}
              <div class="callout">${icon('info', { size: 16 })}<div>Comparisons do not chain like Python. Write <code>0 &lt; x and x &lt; 10</code>, not <code>0 &lt; x &lt; 10</code>.</div></div>
            `)}

            ${chapter(8, 'loops', 'One repeat, three jobs', `
              <p><code>repeat</code> handles fixed counts, ranges and collections. There is no separate <code>for</code> syntax to learn.</p>
              <h3>Repeat a number of times</h3>
              ${codeBlock(`repeat 3 do
    print("again")
end`, { filename: 'loop.luc', lineNumbers: true })}
              <h3>Repeat over a range, with an index</h3>
              ${codeBlock(`repeat 1, 5 as i do
    print(i * i)
end

-- counting down is fine too
repeat -1, 7 as i do
    print(i)
end`, { filename: 'range.luc', lineNumbers: true })}
              <h3>Repeat over a list</h3>
              ${codeBlock(`create names = ["an", "linh", "minh"]

repeat name, i in names do
    print(i + ". " + name)
end`, { filename: 'list-loop.luc', lineNumbers: true })}
              <p><code>while</code> loops until the condition is false, and <code>break</code> leaves early.</p>
              ${codeBlock(`create health = 3

while health > 0 do
    print("health: " + tostring(health))
    health -= 1
end

repeat 1, 100 as n do
    if n == 5 then break end
end`, { filename: 'while.luc', lineNumbers: true })}
              <div class="callout callout-warn">${icon('alert', { size: 16 })}<div><code>repeat</code> does not take a condition. If you need “do this until something is true”, use <code>while</code> (or <code>repeat ... until</code> if you prefer the condition at the bottom).</div></div>
            `)}

            ${chapter(9, 'functions', 'Functions return values', `
              <p>Declare a function with <code>create function</code>. Parameters are local to the body, and <code>return</code> sends a value back to the caller.</p>
              ${codeBlock(`create function add(a, b)
    return a + b
end

create function greet(name, punctuation)
    return "Hello, " + name + punctuation
end

print(add(2, 3))                  -- 5
print(greet("LUC", "!"))          -- Hello, LUC!`, { filename: 'functions.luc', lineNumbers: true })}
              <h3>Defaults and early returns</h3>
              ${codeBlock(`create function area(width, height)
    if height == nil then height = width end
    return width * height
end

print(area(4))       -- 16 (square)
print(area(2, 5))    -- 10

create function safe_div(a, b)
    if b == 0 then return nil end
    return a / b
end`, { filename: 'functions.luc', lineNumbers: true })}
              <h3>Functions are values</h3>
              ${codeBlock(`create double = function(n) return n * 2 end
print(double(21))      -- 42

create function apply(fn, value)
    return fn(value)
end
print(apply(double, 5))   -- 10`, { filename: 'closures.luc', lineNumbers: true })}
              <p>A function that reaches the end without <code>return</code> gives back <code>nil</code>. If you need several values, return a list.</p>
              ${codeBlock(`create function min_max(list)
    list.sort()
    return [list[1], list[len(list)]]
end`, { filename: 'multi.luc', lineNumbers: true })}
            `)}

            ${chapter(10, 'commands', 'Commands run, functions return', `
              <p>LUC separates the two jobs. A <code>command</code> is a procedure: it runs for its effect and cannot return a value. Commands live in the library system, so import <code>libs</code> first.</p>
              ${codeBlock(`import libs

command greet(name)
    print("Hello, " + name)
end

greet("bo")
run greet("zo")     -- explicit form
greet "mo"          -- bare call, no parentheses`, { filename: 'commands.luc', lineNumbers: true })}
              <p>Use <code>command</code> when the function name is a verb and nobody should assign its result. The compiler rejects <code>return</code> inside a command.</p>
              ${codeBlock(`-- this is a compile error:
command bad()
    return 1
end`, { filename: 'commands.luc', lineNumbers: true })}
              <div class="callout callout-tip">${icon('sparkles', { size: 16 })}<div><strong>Under <code>!strict</code></strong>, call commands with <code>run greet("bo")</code>. Bare invocation is reserved for non-strict files.</div></div>
              <p>Commands accept arguments like functions, including optional trailing arguments you check for <code>nil</code>.</p>
            `)}

            ${chapter(11, 'imports', 'Import the batteries', `
              <p>System libraries are imported by name. Give one a short alias by calling the import like a function.</p>
              ${codeBlock(`import json
import net("n")
import window("w")
import discord("bot")
import ai`, { filename: 'imports.luc', lineNumbers: true })}
              <p>Aliases keep library calls short and make it obvious where a name came from.</p>
              ${codeBlock(`import net("n")

create page, code = n.get("https://example.com")
print(code)
print(string.len(page))`, { filename: 'http.luc', lineNumbers: true })}
              <h3>What each library gives you</h3>
              <ul>
                <li><code>json</code> — <code>encode</code>, <code>decode</code>, <code>pretty</code></li>
                <li><code>net</code> — HTTP, TCP, WebSocket clients and servers</li>
                <li><code>window</code> — SDL2 drawing, input, gamepads and sound</li>
                <li><code>ai</code> — tensors, autograd, layers, tokenizers, LM training</li>
                <li><code>discord</code> — bots, slash commands, embeds and buttons</li>
                <li><code>buffer</code> — binary data and typed reads/writes</li>
              </ul>
              ${commandBox('luc --libs', { note: 'Lists core, installed system and user libraries with their status.' })}
              <div class="callout callout-warn">${icon('alert', { size: 16 })}<div>Some libraries need an installer component. If <code>import window</code> fails, run <code>luc install window</code> — same for <code>ai</code> and <code>discord</code>.</div></div>
            `)}

            ${chapter(12, 'libraries', 'Write a library, share one file', `
              <p>LUC has a user-library system built into the core. A library is made of <em>parts</em>. Mark a file as a part with <code>make</code>, then export things by creating them at the top level.</p>
              ${codeBlock(`import libs
make add

create function plus(a, b)
    return a + b
end

create tag = "A"`, { filename: 'add.luc', lineNumbers: true })}
              <p>Bundle several parts into one distributable file with <code>get</code> and <code>pack</code>.</p>
              ${codeBlock(`import libs
get add, sub
pack mymath`, { filename: 'pack.luc', lineNumbers: true })}
              <p>That writes <code>mymath.luic</code> next to your script. Ship that single file. Consumers import from it by part name, or by symbol.</p>
              ${codeBlock(`import libs

import add from mymath
import plus from mymath

print(add.plus(3, 4))    -- 7
print(add.tag)           -- A
print(plus(1, 2))        -- 3`, { filename: 'use.luc', lineNumbers: true })}
              <p><code>get</code> searches next to the script, the working directory, <code>luc_modules/</code> and <code>LUC_PATH</code>.</p>
              <div class="callout callout-tip">${icon('package', { size: 16 })}<div><strong>Ready to share?</strong> Publish it on the <a href="#/publish">package registry</a>. Add a summary, version, license and repo, then copy the install command.</div></div>
            `)}

            ${chapter(13, 'errors', 'Errors without drama', `
              <p><code>error</code> raises, <code>pcall</code> catches. The second return value tells you whether the call succeeded; the third holds the error.</p>
              ${codeBlock(`create ok, err = pcall(function()
    error("the thing broke")
end)

if not ok then
    print("caught: " + tostring(err))
end`, { filename: 'errors.luc', lineNumbers: true })}
              <p><code>assert</code> is the short form for “this must be true”:</p>
              ${codeBlock(`create function divide(a, b)
    assert(b != 0, "cannot divide by zero")
    return a / b
end

create ok, err = pcall(divide, 1, 0)
print(ok)       -- false
print(err)      -- cannot divide by zero`, { filename: 'assert.luc', lineNumbers: true })}
              <p>Use <code>tostring</code> on an error before concatenating it. Errors carry the source line, so the message already tells you where to look.</p>
              <div class="callout">${icon('info', { size: 16 })}<div>Prefer returning <code>nil, "reason"</code> for expected failures such as a missing file, and reserve <code>error</code> for bugs and broken invariants.</div></div>
            `)}

            ${chapter(14, 'strict', 'Strict mode and exact math', `
              <p>Two file-level flags change how the whole file compiles. They must appear at the top.</p>
              ${codeBlock(`!strict

create name = "luc"
create config = {"theme": "dark"}   -- keys must be quoted

print(config.theme)`, { filename: 'strict.luc', lineNumbers: true })}
              <p>Under <code>!strict</code>:</p>
              <ul>
                <li>Every variable must be declared with <code>create</code></li>
                <li>Dict keys must be quoted strings</li>
                <li>Bare <code>function</code> and <code>command</code> declarations are rejected — use <code>create function</code> and <code>create command</code></li>
                <li><code>run</code> is required to call a command</li>
                <li><code>True</code>, <code>False</code> and <code>None</code> are compile errors</li>
              </ul>
              <p><code>!karatsuba</code> switches integer-string multiplication to an exact algorithm, also available explicitly:</p>
              ${codeBlock(`!karatsuba

create huge = "123456789012345678901234567890"
create product = math.karatsuba(huge, huge)
print(product)`, { filename: 'bigmath.luc', lineNumbers: true })}
              <div class="callout callout-tip">${icon('shield', { size: 16 })}<div>Use <code>!strict</code> in libraries you publish and in scripts longer than a screen. Prototypes can stay loose.</div></div>
            `)}

            ${chapter(15, 'next', 'Where to go next', `
              <p>You now know the core of the language: declarations, collections, loops, functions, commands, imports and libraries. The fastest way to make it stick is to build one small thing end to end.</p>
              <h3>Three projects that teach different parts</h3>
              <ul>
                <li><strong>A terminal tool.</strong> Read a file, parse JSON with <code>json</code>, print a table. Try the <code>term</code> package for colors.</li>
                <li><strong>A window game.</strong> Start with the bouncing-circle demo and add input. Use <code>import window("w")</code>.</li>
                <li><strong>A Discord bot.</strong> Copy the ping demo, swap in your token, add one new command. Mind the message-content intent.</li>
              </ul>
              <p>When you have written something reusable, turn it into a library part and <a href="#/publish">publish it</a>. Reading other people's packages is the next best way to learn the idioms.</p>
              <div class="guide-end">
                <h2>Keep going</h2>
                <p>Browse the registry, read a short package, or ask a question in the community. Every link opens in place.</p>
                <div class="guide-end-actions">
                  <a class="btn btn-primary btn-sm" href="#/packages">${icon('package', { size: 15 })} Browse packages</a>
                  <a class="btn btn-ghost btn-sm" href="#/community">${icon('users', { size: 15 })} Community</a>
                  <a class="btn btn-ghost btn-sm" href="https://github.com/hsusulist/luc/tree/main/demos" target="_blank" rel="noopener noreferrer">${icon('code', { size: 15 })} Demos on GitHub</a>
                </div>
              </div>
            `)}
          </div>
        </div>
      </div>
    `,
    after() {
      observeReveal();

      const progress = document.getElementById('guide-progress');
      const links = [...document.querySelectorAll('.guide-toc a')];
      const sections = chapters
        .map((c) => document.getElementById(c.id))
        .filter(Boolean);

      if (!progress || !sections.length) return;

      const onScroll = () => {
        const max = document.documentElement.scrollHeight - window.innerHeight;
        const ratio = max > 0 ? Math.min(1, Math.max(0, window.scrollY / max)) : 0;
        progress.style.width = `${ratio * 100}%`;

        let current = sections[0].id;
        for (const section of sections) {
          if (section.getBoundingClientRect().top <= 150) current = section.id;
        }
        links.forEach((link) => {
          link.classList.toggle('is-active', link.dataset.scroll === current);
        });
      };

      window.addEventListener('scroll', onScroll, { passive: true });
      onScroll();

      return () => {
        window.removeEventListener('scroll', onScroll);
        if (progress) progress.style.width = '0%';
      };
    }
  };
}
