'use strict';

/*
 * Seed registry for the LUC website.
 * This mirrors real modules that ship with LUC today, plus a few example
 * community packages so the registry page has something to show.
 */

const history = (base) => {
  const wobble = [0.78, 0.86, 0.81, 0.95, 1.0, 0.92, 1.08];
  return wobble.map((w) => Math.round(base * w));
};

const packages = [
  {
    slug: 'window',
    name: 'window',
    summary: 'SDL2 graphics, input, gamepads and sound in a handful of calls. Put a real window on screen without leaving LUC.',
    version: '1.4.2',
    license: 'Apache-2.0',
    repo: 'https://github.com/hsusulist/luc/tree/main/src',
    homepage: 'https://github.com/hsusulist/luc',
    author: { name: 'hsusulist', handle: 'hsusulist', url: 'https://github.com/hsusulist', verified: true },
    official: true,
    featured: true,
    tags: ['graphics', 'gamedev', 'sound', 'sdl2'],
    accent: '#ff7a18',
    size: '1.2 MB',
    weeklyDownloads: 15210,
    totalDownloads: 284900,
    downloadsHistory: history(15210),
    publishedAt: '2026-03-14',
    updatedAt: '2026-09-18',
    dependencies: {},
    versions: [
      { version: '1.4.2', date: '2026-09-18', notes: 'Gamepad hot-plug fixes, stable touch IDs, faster sprite batching.' },
      { version: '1.4.0', date: '2026-07-02', notes: 'Added OGG/MP3 streaming and display scaling helpers.' },
      { version: '1.3.1', date: '2026-05-19', notes: 'Clipboard, message boxes and open_url.' }
    ],
    readme: `## window

SDL2 graphics and sound for LUC. One import, then draw.

\`\`\`luc
import window("w")

create x, vx = 100, 200

w.go("Hello LUC", 800, 600, function(dt)
    x = x + vx * dt
    if x > 780 or x < 20 then vx = -vx end

    w.clear("navy")
    w.circle(x, 300, 30, "yellow")
    w.text_center("Xin chao LUC!", 100, "white", 32)
end)
\`\`\`

### What is inside

- Shapes: \`pixel\`, \`line\`, \`rect\`, \`circle\`, \`arc\`, \`ellipse\`, \`triangle\`, \`polygon\`
- Text and sprites: \`text\`, \`text_center\`, \`image\`, \`sprite\`
- Input: keyboard, mouse, wheel, text input, touch and gamepads
- Sound: \`play\`, \`stop\`, \`pause_sound\` for WAV, OGG and MP3
- Window control: fullscreen, resize, minimize, clipboard, message boxes

### Install

\`\`\`sh
luc install window
\`\`\`

Requires the \`window\` component from the LUC installer. Rendering runs on the
main thread and the callback receives the delta time in seconds.`,
  },
  {
    slug: 'ai',
    name: 'ai',
    summary: 'A mini-PyTorch for LUC: tensors, autograd, layers, optimizers, tokenizers and a trainable language model on CPU.',
    version: '1.2.0',
    license: 'Apache-2.0',
    repo: 'https://github.com/hsusulist/luc/tree/main/luc_modules',
    homepage: 'https://github.com/hsusulist/luc',
    author: { name: 'hsusulist', handle: 'hsusulist', url: 'https://github.com/hsusulist', verified: true },
    official: true,
    featured: true,
    tags: ['ai', 'machine-learning', 'tensor', 'neural-network'],
    accent: '#8b5cf6',
    size: '452 KB',
    weeklyDownloads: 12480,
    totalDownloads: 187300,
    downloadsHistory: history(12480),
    publishedAt: '2026-04-02',
    updatedAt: '2026-09-18',
    dependencies: {},
    versions: [
      { version: '1.2.0', date: '2026-09-18', notes: 'LMTrain CPU preset is ~2x faster; new tokenizer training options.' },
      { version: '1.1.0', date: '2026-06-11', notes: 'Added forge autograd engine and rope embeddings.' },
      { version: '1.0.0', date: '2026-04-02', notes: 'First stable port of lanternl to LUC.' }
    ],
    readme: `## ai

Lanternl, a tiny PyTorch, ported to LUC. Logic unchanged — just \`.luc\` files.

\`\`\`luc
import ai

create data = [
    {"input": [0], "target": [1]},
    {"input": [1], "target": [3]},
    {"input": [2], "target": [5]},
    {"input": [3], "target": [7]},
]

create t = ai.Train({
    "layers": {1: 1, 2: 8, 3: 1},
    "data": data,
    "epochs": 300,
    "lr": 0.01,
    "log_every": 100
})
t:run()
t:evaluate(data)
\`\`\`

### Text models

\`\`\`luc
import ai

create model = ai.LMTrain {
    data = "hello world, this is luc ai!",
    preset = "auto",
    epochs = 300
}
model:run()
print(model:generate("hello", 20))
\`\`\`

### Modules

\`nn\`, \`optim\`, \`rope\`, \`positional\`, \`tokenizer\`, \`transformer\`, \`matrix\`,
\`tensor\`, \`forge\` and \`data\`. GPU modules auto-disable when no bridge is
present, so everything above runs on CPU with no setup.

### Install

\`\`\`sh
luc install ai
\`\`\`

Put \`luc_modules/\` next to your script, or point \`LUC_PATH\` at it.`,
  },
  {
    slug: 'discord',
    name: 'discord',
    summary: 'Build chat and slash-command bots with embeds, buttons and menus. Batteries included, token required.',
    version: '0.6.0',
    license: 'Apache-2.0',
    repo: 'https://github.com/hsusulist/luc/tree/main/luc_modules',
    homepage: '',
    author: { name: 'hsusulist', handle: 'hsusulist', url: 'https://github.com/hsusulist', verified: true },
    official: true,
    featured: false,
    tags: ['bots', 'discord', 'chat'],
    accent: '#5865f2',
    size: '34 KB',
    weeklyDownloads: 8340,
    totalDownloads: 96300,
    downloadsHistory: history(8340),
    publishedAt: '2026-03-20',
    updatedAt: '2026-09-21',
    dependencies: { net: '^1.1.0' },
    versions: [
      { version: '0.6.0', date: '2026-09-21', notes: 'Discord embeds and button components.' },
      { version: '0.5.0', date: '2026-08-04', notes: 'Slash commands and autocomplete.' },
      { version: '0.4.0', date: '2026-05-23', notes: 'Gateway reconnect and rate limits.' }
    ],
    readme: `## discord

Chat bots, slash commands, embeds and buttons.

\`\`\`luc
import discord("bot")

create token = bot.token("TOKEN_HERE")

bot:run(token)
while bot.run do
    create cmd = bot.prefix("!")
    if cmd == "ping" then
        bot.msg:send("pong!")
    end
end
\`\`\`

### Setup

1. Create a bot at \`discord.com/developers/applications\`
2. Turn on **MESSAGE CONTENT INTENT** on the Bot page
3. Invite it with the \`bot\` scope
4. Paste the token and run:

\`\`\`sh
luc install discord
luc bot.luc
\`\`\`

> Never commit a bot token. Read it with \`os.getenv("DISCORD_TOKEN")\` instead.

Slash commands, embeds and buttons are available through \`bot.slash\`,
\`bot.embed\` and \`bot.button\`.`,
  },
  {
    slug: 'net',
    name: 'net',
    summary: 'TCP, HTTP and WebSocket client + server. Fetch a page in one line, serve one in ten.',
    version: '1.1.0',
    license: 'Apache-2.0',
    repo: 'https://github.com/hsusulist/luc/tree/main/src',
    homepage: '',
    author: { name: 'hsusulist', handle: 'hsusulist', url: 'https://github.com/hsusulist', verified: true },
    official: true,
    featured: false,
    tags: ['http', 'networking', 'websocket', 'server'],
    accent: '#38d996',
    size: '96 KB',
    weeklyDownloads: 9180,
    totalDownloads: 121400,
    downloadsHistory: history(9180),
    publishedAt: '2026-02-28',
    updatedAt: '2026-08-30',
    dependencies: {},
    versions: [
      { version: '1.1.0', date: '2026-08-30', notes: 'WebSocket fragmentation, server send helpers.' },
      { version: '1.0.0', date: '2026-05-10', notes: 'TLS by default, HTTP/1.1 keep-alive.' }
    ],
    readme: `## net

TCP, HTTP and WebSockets, built into the interpreter.

\`\`\`luc
import net("n")

create page, code = n.get("https://example.com")
print(code)
print(string.len(page))
\`\`\`

### POST JSON

\`\`\`luc
import net("n")
import json

create body = json.encode({"name": "luc"})
create res, code = n.post("https://httpbin.org/post", body)
print(code)
\`\`\`

### Server

\`\`\`luc
import net("n")

n.serve(8080, function(req)
    return 200, "hello from luc"
end)
\`\`\`

TLS certificates are verified by default. See \`demos/net_libaries/\` for a
multi-client chat server.`,
  },
  {
    slug: 'libs',
    name: 'libs',
    summary: 'The user-library system. Declare a part with make, collect parts with get, and ship one .luic file with pack.',
    version: '0.3.1',
    license: 'Apache-2.0',
    repo: 'https://github.com/hsusulist/luc/tree/main/demos/libs_libaries',
    homepage: '',
    author: { name: 'hsusulist', handle: 'hsusulist', url: 'https://github.com/hsusulist', verified: true },
    official: true,
    featured: true,
    tags: ['libraries', 'tooling', 'packaging'],
    accent: '#ffb066',
    size: 'core',
    weeklyDownloads: 21600,
    totalDownloads: 412700,
    downloadsHistory: history(21600),
    publishedAt: '2026-02-10',
    updatedAt: '2026-08-12',
    dependencies: {},
    versions: [
      { version: '0.3.1', date: '2026-08-12', notes: 'pack now writes next to the script; faster get lookup.' },
      { version: '0.3.0', date: '2026-06-02', notes: 'import-from for whole parts and single symbols.' }
    ],
    readme: `## libs

The user-library system is part of the core interpreter. Import it, then use
\`make\`, \`get\`, \`pack\` and \`command\`.

### Write a part

\`\`\`luc
import libs
make add

create function plus(a, b)
    return a + b
end

create tag = "A"
\`\`\`

Every top-level \`create\` is exported automatically.

### Bundle parts

\`\`\`luc
import libs
get add, sub
pack mymath
\`\`\`

This writes \`mymath.luic\` next to your script. Share that single file.

### Use a bundle

\`\`\`luc
import libs
import add from mymath
import plus from mymath

print(add.plus(3, 4))  -- 7
print(plus(1, 2))      -- 3
\`\`\`

\`get\` searches next to your script, the working directory, \`luc_modules/\`
and \`LUC_PATH\`.`,
  },
  {
    slug: 'json',
    name: 'json',
    summary: 'Fast encode and decode with stable key order, pretty printing and safe number handling.',
    version: '0.4.0',
    license: 'Apache-2.0',
    repo: 'https://github.com/hsusulist/luc/tree/main/src',
    homepage: '',
    author: { name: 'hsusulist', handle: 'hsusulist', url: 'https://github.com/hsusulist', verified: true },
    official: true,
    featured: false,
    tags: ['json', 'parsing', 'data'],
    accent: '#5aa9ff',
    size: 'core',
    weeklyDownloads: 6720,
    totalDownloads: 88900,
    downloadsHistory: history(6720),
    publishedAt: '2026-03-01',
    updatedAt: '2026-07-14',
    dependencies: {},
    versions: [
      { version: '0.4.0', date: '2026-07-14', notes: 'Pretty printer and array/object detection.' },
      { version: '0.3.2', date: '2026-04-22', notes: 'Correct escapes for control characters.' }
    ],
    readme: `## json

JSON in and out, no dependency required.

\`\`\`luc
import json

create doc = json.decode('{"name": "luc", "fast": true}')
print(doc.name)
print(json.encode(doc))
print(json.pretty(doc))
\`\`\`

\`decode\` returns lists for arrays and dicts for objects. Invalid input raises
an error you can catch with \`pcall\`.`,
  },
  {
    slug: 'tester',
    name: 'tester',
    summary: 'A tiny, zero-config test runner: describe/it blocks, assertions and a readable summary for your LUC libraries.',
    version: '0.2.3',
    license: 'MIT',
    repo: 'https://github.com/hsusulist/luc/issues',
    homepage: '',
    author: { name: 'mika', handle: 'mikathedev', url: 'https://github.com/hsusulist', verified: false },
    official: false,
    featured: false,
    tags: ['testing', 'tooling', 'devtools'],
    accent: '#f472b4',
    size: '18 KB',
    weeklyDownloads: 2410,
    totalDownloads: 19300,
    downloadsHistory: history(2410),
    publishedAt: '2026-05-09',
    updatedAt: '2026-08-02',
    dependencies: { libs: '^0.3.0' },
    versions: [
      { version: '0.2.3', date: '2026-08-02', notes: 'Focus on a single test with --only.' },
      { version: '0.2.0', date: '2026-06-18', notes: 'assert.deep and timing output.' }
    ],
    readme: `## tester

Write tests next to your library parts.

\`\`\`luc
import libs
import tester

tester.describe("math", function()
    tester.it("adds", function()
        tester.expect(1 + 1).to_be(2)
    end)

    tester.it("compares lists", function()
        tester.expect([1, 2]).to_equal([1, 2])
    end)
end)

tester.run()
\`\`\`

### Output

\`\`\`text
math
  ok   adds
  ok   compares lists

2 passed, 0 failed in 3ms
\`\`\`

Run \`tester.run()\` at the bottom of your file. Exit code 1 on failure, so it
works in CI.`,
  },
  {
    slug: 'dotenv',
    name: 'dotenv',
    summary: 'Load .env files into os.getenv without touching your shell. Tiny, no dependencies, safe with quotes.',
    version: '0.1.8',
    license: 'MIT',
    repo: 'https://github.com/hsusulist/luc/issues',
    homepage: '',
    author: { name: 'linh', handle: 'linhbuilds', url: 'https://github.com/hsusulist', verified: false },
    official: false,
    featured: false,
    tags: ['config', 'environment', 'tooling'],
    accent: '#ffd166',
    size: '6 KB',
    weeklyDownloads: 1890,
    totalDownloads: 11400,
    downloadsHistory: history(1890),
    publishedAt: '2026-06-21',
    updatedAt: '2026-08-16',
    dependencies: {},
    versions: [
      { version: '0.1.8', date: '2026-08-16', notes: 'Support inline comments and quoted values.' },
      { version: '0.1.0', date: '2026-06-21', notes: 'First release.' }
    ],
    readme: `## dotenv

Keep secrets out of your source.

\`\`\`luc
import dotenv

dotenv.load()                    -- reads .env next to the script
dotenv.load(".env.local")        -- or a specific file

create token = os.getenv("DISCORD_TOKEN")
if token == nil then
    error("DISCORD_TOKEN is missing")
end
\`\`\`

### Supported syntax

\`\`\`text
# comment
API_URL=https://example.com
QUOTED="value with spaces"
SINGLE='single quotes work too'
\`\`\`

Existing environment variables are never overwritten unless you pass \`true\` as
the second argument to \`load\`.`,
  },
  {
    slug: 'term',
    name: 'term',
    summary: 'Colors, boxes, tables, spinners and prompts for terminal apps. Looks great on Windows Terminal and every ANSI shell.',
    version: '0.9.0',
    license: 'Apache-2.0',
    repo: 'https://github.com/hsusulist/luc/issues',
    homepage: '',
    author: { name: 'phuc', handle: 'phuc-codes', url: 'https://github.com/hsusulist', verified: false },
    official: false,
    featured: false,
    tags: ['cli', 'terminal', 'ui'],
    accent: '#7dd3fc',
    size: '22 KB',
    weeklyDownloads: 3260,
    totalDownloads: 27800,
    downloadsHistory: history(3260),
    publishedAt: '2026-04-27',
    updatedAt: '2026-09-02',
    dependencies: {},
    versions: [
      { version: '0.9.0', date: '2026-09-02', notes: 'Truecolor detection and table column alignment.' },
      { version: '0.8.0', date: '2026-07-08', notes: 'Spinner and progress bar.' }
    ],
    readme: `## term

Make command-line output feel finished.

\`\`\`luc
import term

term.write("ready", "green")
term.box("LUC", "build complete in 1.2s")

term.table(
    ["name", "version"],
    [["ai", "1.2.0"], ["window", "1.4.2"]]
)

create sp = term.spinner("installing")
sp:start()
os.sleep(1)
sp:stop("done", "green")
\`\`\`

Colors accept names (\`red\`, \`green\`, \`yellow\`, \`blue\`, \`magenta\`, \`cyan\`,
\`white\`, \`gray\`) or a \`#rrggbb\` value. ANSI is disabled automatically when
output is redirected.`,
  },
  {
    slug: 'luc-web',
    name: 'luc-web',
    importName: 'lucweb',
    summary: 'A minimal HTTP framework: routes with params, middleware, static files and JSON responses. Built on net.',
    version: '0.1.0',
    license: 'MIT',
    repo: 'https://github.com/hsusulist/luc/issues',
    homepage: '',
    author: { name: 'an', handle: 'anwritescode', url: 'https://github.com/hsusulist', verified: false },
    official: false,
    featured: false,
    tags: ['web', 'http', 'server'],
    accent: '#a3e635',
    size: '31 KB',
    weeklyDownloads: 1520,
    totalDownloads: 9400,
    downloadsHistory: history(1520),
    publishedAt: '2026-07-30',
    updatedAt: '2026-09-09',
    dependencies: { net: '^1.1.0', json: '^0.4.0' },
    versions: [
      { version: '0.1.0', date: '2026-09-09', notes: 'First public beta: routing, params, static files.' },
      { version: '0.0.9', date: '2026-07-30', notes: 'Internal preview.' }
    ],
    readme: `## luc-web

A small router on top of \`net\`, inspired by the clarity of LUC itself.

\`\`\`luc
import lucweb("app")

app.get("/", function(req)
    return app.html("<h1>Hello from LUC</h1>")
end)

app.get("/users/:id", function(req)
    return app.json({"id": req.params.id, "name": "anonymous"})
end)

app.static("/public", "./public")
app.listen(3000)
\`\`\`

> **Beta.** The API can change between 0.x releases. Documentation and more
> examples are in the package repository.

### Roadmap

- Middleware chains
- Cookie helpers
- Template rendering
- Better 404/500 pages`,
  }
];

module.exports = { packages };
