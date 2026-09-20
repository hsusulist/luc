# LUC Programming Language

<!-- update 2026-09-02: ver 0.1, library management, offline luc install -->

<p align="center">
  <img src="vscode/icons/luc.svg" width="120" alt="LUC Logo"/>
</p>

<p align="center">
  A fast, lightweight scripting language — easy like Lua, with more features built in.
</p>

<p align="center">
  <img src="https://img.shields.io/badge/version-0.1-orange" />
  <img src="https://img.shields.io/badge/platform-Windows-blue" />
  <img src="https://img.shields.io/badge/license-Apache%202.0-green" />
  <img src="https://img.shields.io/badge/built%20with-C99-lightgrey" />
</p>

---

##  Install

**Windows** — download and run:

**[luc-installer.exe](https://github.com/hsusulist/luc/releases/latest)**

Double click → choose components → Install → Done.  
Open any terminal and type `luc --version` to verify.

### What gets installed

The Components page keeps the install light by default:

| Component | Default | What it is |
|---|---|---|
| **main** | ✔ always | LUC interpreter + core libraries (string, list, math, bit32, JSON, buffer, IO, OS, task, coroutine, **net** built-in) |
| **window** | ☐ opt-in | SDL2 2D graphics, PNG/JPG sprites, TTF text, WAV/OGG/MP3 sound + demos |
| **ailib** | ☐ opt-in | lanternl AI library — `import ai` |
| **discord** | ☐ opt-in | Discord bot library — `import discord` (needs `luc install discord` if skipped) |
| **vsext** | ✔ | VS Code extension (syntax highlighting) |
| **source** | ✔ | C source code for developers |

Skipped `window`/`ailib`/`discord` and changed your mind later? No installer needed —
see [`luc install`](#luc-install--package-manager) below.

### Re-running the installer (maintenance)

Run `luc-installer.exe` again — or open **Luc Installer** straight from the
Start Menu — and it detects the existing install, then offers three choices:

| Option | What it does |
|---|---|
| **Install libraries** | opens the components page pre-ticked with your last selection: tick to add a library, **untick to remove it** |
| **Fix** | repair: silently reinstall all program files over the current folder, settings kept |
| **Uninstall** | remove LUC completely |

---

## luc install — package manager

The installer bundles a `packages\` folder next to `luc.exe`, so this works
**offline**; GitHub is only used when the local package is missing.

```
luc install                  list packages and their status
luc install window           add SDL2 window support (full build + media DLLs + font)
luc install ai               add the lanternl AI library (import ai)
luc install discord          add the Discord bot library (import discord)
luc install window --force   redownload window support
```

> **net** needs no download: it is built into `luc.exe` (Winsock, no extra
> DLLs) — `import net` works right after install.

- `luc install window` replaces `luc.exe` with the SDL2 build and drops
  `SDL2.dll` + media DLLs (`SDL2_ttf/image/mixer` + deps) + `DejaVuSans.ttf` font next to it (the old exe is kept as `luc.exe.old`).
- `luc install ai` unpacks the `ai.lucpkg` bundle into `luc_modules\`,
  after which `import ai` works from any folder.
- Local source: `packages\` in the install folder (shipped by the installer).
- Online fallback: `raw.githubusercontent.com/hsusulist/luc/main`.
  Set `LUC_INSTALL_URL` to point at a mirror or a local test server.

### Rebuilding the ai bundle

After changing files in `luc_modules\`, regenerate the package and push:

```powershell
powershell -File tools\make_pkg.ps1
```

`luc install ai` picks the updated `packages\ai.lucpkg` up immediately.

---

## Quick Start

Create a file `hello.luc`:

```lua
print("Hello from LUC!")

-- list (Python style)
create fruits = ["apple", "banana", "orange"]
fruits:append("mango")
print("Fruits: " .. fruits:len())

-- task
task.spawn(function()
    task.wait(1)
    print("1 second later!")
end)
```

Run it:
```
luc hello.luc
```

---

## 🪟 Window / GUI

`window` là **thư viện hệ thống** của LUC — nạp bằng `import` (không phải `require`).
Cần bản window: `luc install window` (SDL2 + font + sound).

Cách dễ nhất cho người mới — `window.go()` lo hết vòng lặp, bạn chỉ viết code vẽ:

```lua
import window("w")

create x, vx = 100, 200

w.go("Game Cua To", 800, 600, function(dt)
    x = x + vx * dt
    if x > 780 or x < 20 then vx = -vx end

    w.clear("navy")
    w.circle(x, 300, 30, "yellow")
    w.text_center("Xin chào LUC! Có dấu tiếng Việt.", 100, "white", 32)
end)
```

Muốn tự điều khiển vòng lặp (như demo Pong) thì dùng `start / running / update / close`
cũng được — cả hai cách đều chạy.

```lua
-- hình ảnh: PNG/JPG/GIF (AVIF/JPEG-XL không ship kèm)
create hero = w.sprite("hero.png")
w.draw(hero, x, y)
w.draw(hero, x, y, { "scale": 2, "rotate": 45, "flip": "x", "alpha": 128, "center": true })

-- chữ: font TTF mặc định gõ được tiếng Việt; thiếu màu thì lấy trắng
w.text("Điểm: 10", 10, 10, "white", 24)
w.text_center("TẠM DỪNG", 200, "gold", 40)
w.font("myfont.ttf", 20)   -- font riêng (tùy chọn)

-- âm thanh: WAV luôn chạy; OGG/MP3/FLAC chạy khi đủ DLL đi kèm
create jump = w.sound("jump.wav")
w.play(jump)
w.play(jump, { "loop": 2, "volume": 80 })
create bgm = w.music("nhac.ogg")
w.play_music(bgm)          -- lặp vô hạn mặc định
w.stop_music()

-- phím/chuột: w.key("space"), w.key_pressed("escape"), w.mouse() ...
-- xem them: demos/window libaries/pong.luc
```

> **import vs require** — `import` chỉ nạp thư viện hệ thống (`window`, `ai`, `json`).
> Module của bên thứ ba nạp bằng `require`: `create mylib = require("mylib")` —
> và `require("window")` sẽ luôn lấy module `window` của bạn (nếu có), không phải của hệ thống.

---

##  AI library (lanternl)

lanternl — a mini-PyTorch written in pure Lua — is ported to LUC and ships
as the optional `ailib` component (or via `luc install ai`):

```lua
import ai
print(type(ai.Tensor))       -- table
print(type(ai.LMTrain))      -- table
print(type(ai.Tokenizer))    -- table
```

---

##  Network (net)

TCP + HTTP client/server, built in (Winsock on Windows, no extra DLLs):

```lua
import net("n")

-- HTTP: one line
create page, code = n.get("http://example.com")
print(code)   -- 200

-- echo server: one task per client, blocking-style recv just works
create s = n.serve(8000)
while true do
    create cli = s:accept()
    task.spawn(function()
        while true do
            create m = cli:recv()
            if m == nil then break end
            cli:send("echo:" .. m)
        end
        cli:close()
    end)
end
```

`n.connect(host, port, timeout?)`, `cli:send/recv/close`, `recv` returns
`nil, "closed"` on disconnect and `nil, "timeout"` on timeout.
`http://` and `https://` both work, plus `n.ws_connect(url)` WebSocket
(`ws://`/`wss://`, ping auto-answered, fragmented messages reassembled).

---

##  Discord bot (discord)

Needs a bot token (https://discord.com/developers/applications, turn on
MESSAGE CONTENT INTENT to read text) and the library itself:

```
luc install discord
```

Setup — token riêng, run gọn (`bot:run` chỉ chạy bot, không loop).
Chạy không được (token sai, mất mạng...) thì nó ném lỗi luôn nên code
sau đó chỉ chạy khi đã online. Token có thể truyền trực tiếp hoặc nạp
trước bằng `bot.token`:

```lua
import discord("bot")

create log = bot.debug
create token = bot.token("TOKEN_HERE")

bot:run(token)
print("login as " + log.username + " and in " + log.servers + " servers")

while bot.run do
    create cmd = bot.prefix("!")
    if cmd == "ping" then
        bot.msg:send("pong!")
    end
end
```

Debug — `bot.debug` lấy trước khi run cũng được (cùng một bảng nên
tự cập nhật khi online): `log.username` (vd `bottesting`), `log.name`
(tên hiển thị), `log.id`, `log.servers` (số server đang ở, tự cập nhật
khi vào/rời server).

Lib im lặng mặc định; bật `LUC_WSDEBUG=1` để xem traffic (`discord> ...`).

Prefix — `bot.prefix("!")` chờ tới khi có người nhắn `!...` rồi trả chữ
đằng sau nó (`!ping` thì `cmd == "ping"`). Mỗi prefix một watcher riêng
nên không trùng nhau (mỗi watcher một task):

```lua
task.spawn(function()
    while true do
        if bot.prefix("!") == "ping" then msg:send("pong!") end
    end
end)
task.spawn(function()
    while true do
        if bot.prefix("?") == "ping" then msg:send("pong?") end
    end
end
```

```lua
while bot.run do
    create cmd = bot.prefix("!")
    if cmd == "ping" then
        bot.msg:send("pong!")
    end
end
```

Msg — gom `bot.msg` lại cho gọn. `send` bằng tên channel, `dm` bằng
tên hoặc id. Một arg thì trả lời ngay tại chỗ ra lệnh:

```lua
create msg = bot.msg
msg:send("general", "pong!")
msg:send("pong!")
msg.dm("someuser", "chào bạn")
```

Đọc tin nhắn cũ (không block như prefix — gọi lúc nào trả lúc đó):

```lua
create text, who = msg:last("general")       -- tin mới nhất kênh
create text = msg:lastby("someuser")         -- text mới nhất của user (mọi kênh)
create text = msg:lastby("someuser", "general")
create name = msg:sender("!ping")            -- ai nhắn đúng text này
```

Trong prefix/slash command thì bỏ args cũng được — tự lấy người gọi
lệnh và kênh họ nhắn: `msg:lastby()` (text mới nhất của người đó tại
đó), `msg:last()` (tin mới nhất tại đó). Đừng gọi trong vòng lặp sát
nhau (mỗi gọi là 1+ request REST).

User — `join()` chờ người mới vào trả id, `getid(tên)` đổi tên ra id:

```lua
create user = bot.user
task.spawn(function()
    while true do
        create uid = user:join()
        msg.dm(uid, "Welcome to the server")
    end
end)
```

Slash — `cmd.new` tạo lệnh, `cmd.slash("hi")` chạy khi có người gõ `/hi`.
`bot:stop()` kìm lại (vẫn nhận nhưng giữ queue), `bot:on()` xả hết ra:

```lua
create cmd = bot.slash
cmd.new("hi", "gobal")
while bot.on do
    if cmd.slash("hi") then
        create msg = bot.msg
        msg:send("hi")
    end
end
```

See `demos/discord libaries/discordbot.luc`.

---

## Built-in Libraries

| Library | Description |
|--|--|
| `string` | split, trim, tohex, fromhex, upper, lower... |
| `table` | insert, remove, concat, move, sort |
| `math` | floor, ceil, sqrt, sin, cos, random... |
| `io` | read, write, open, popen, replace (terminal overwrite) |
| `os` | clock, time, sleep, execute, getenv |
| `task` | spawn, wait, delay, cancel |
| `buffer` | binary data, hex read/write |
| `bit32` | bitwise operations |
| `coroutine` | create, resume, yield, wrap |
| `require("json")` | encode, decode |
| `import window` | GUI windows, drawing, input (`import window("w")` để đặt tên ngắn) |
| `import net` | TCP + HTTP(S) client/server, WebSocket (`import net("n")` để đặt tên ngắn) |
| `import discord` | Discord bot (`luc install discord` first, needs a bot token) |

---

## Syntax

LUC syntax is based on Lua 5.1 with one twist: variables are declared with **`create`** instead of `local` (`create x = 10` is a local, plain `x = 10` makes a global).  
Extra features on top:

```lua
-- Type annotations (optional, zero cost)
create x: number = 10
create name: string = "LUC"

-- List (Python style, 1-indexed like Lua)
create arr = [1, 2, 3]
arr:append(4)
arr:pop()
print(arr:len())   -- 3

-- 'in' operator
if 2 in arr then
    print("found!")
end

-- String extensions
print(string.split("a,b,c", ","):len())  -- 3
print(string.tohex("LUC"))              -- 4c5543
print(string.upper("hello"))            -- HELLO

-- JSON
create json = require("json")
create t = json.decode('{"x":1}')
print(t.x)                              -- 1
print(json.encode({name="LUC"}))        -- {"name":"LUC"}

-- Task / async
task.spawn(function()
    task.wait(1)
    print("async!")
end)

-- Terminal animation
for i = 1, 100 do
    io.replace("Loading: " .. i .. "%")
    task.wait(0.05)
end
io.write("\n")
```

---

##  Build from Source

**Requirements:** gcc (MinGW on Windows), SDL2 (optional, for window library)

```bash
# Console build (no window)
gcc -O2 -std=gnu99 -o luc-core src/luc_core.c src/luc_libs.c src/luc_trans.c -lm -lws2_32

# Window build (Windows MinGW)
gcc -O2 -std=gnu99 -DLUC_WINDOW -o luc.exe src/luc_core.c src/luc_libs.c src/luc_trans.c -lm -lSDL2 -lwinhttp -lws2_32

# Window build (Linux)
gcc -O2 -std=gnu99 -DLUC_WINDOW -o luc src/luc_core.c src/luc_libs.c src/luc_trans.c -lm -lSDL2
```

---

##  Project Structure

```
luc/
├── build_installer.ps1  ← builds dist\luc-installer.exe (Windows)
├── src/                 ← luc_core.c + luc_libs.c (C99)
├── luc_modules/         ← lanternl AI library ported to LUC (import ai)
├── packages/            ← ai.lucpkg bundle consumed by "luc install ai"
├── tools/               ← make_pkg.ps1 (rebuild the ai bundle)
├── demos/               ← examples per lib: ai, discord, net, window (subfolders)
├── vscode/              ← VS Code extension (syntax highlighting + icons)
├── installer/           ← Inno Setup script + assets
├── dist/                ← compiled binaries + luc-installer.exe
├── minesweeper.luc      ← minesweeper game (window lib demo)
├── LICENSE
└── README.md
```

---

##  License

Apache License 2.0 — see [LICENSE](LICENSE)

---

<p align="center">Made with ❤️ by <a href="https://github.com/hsusulist">hsusulist</a></p>

