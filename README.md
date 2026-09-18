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
| **main** | ✔ always | LUC interpreter + core libraries (string, list, math, bit32, JSON, buffer, IO, OS, task, coroutine) |
| **window** | ☐ opt-in | SDL2 2D graphics, PNG/JPG sprites, TTF text, WAV/OGG/MP3 sound + demos |
| **ailib** | ☐ opt-in | lanternl AI library — `import ai` |
| **vsext** | ✔ | VS Code extension (syntax highlighting) |
| **source** | ✔ | C source code for developers |

Skipped `window`/`ailib` and changed your mind later? No installer needed —
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
luc install window --force   redownload window support
```

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
-- xem them: demos/pong.luc
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
gcc -O2 -std=c99 -o luc-core src/luc_core.c src/luc_libs.c -lm

# Window build (Windows MinGW)
gcc -O2 -std=c99 -DLUC_WINDOW -o luc.exe src/luc_core.c src/luc_libs.c -lm -lSDL2 -lwinhttp

# Window build (Linux)
gcc -O2 -std=c99 -DLUC_WINDOW -o luc src/luc_core.c src/luc_libs.c -lm -lSDL2
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
├── demos/               ← example .luc files (hello, json, pong, minesweeper)
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

