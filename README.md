# LUC Programming Language

<!-- update 2026-09-02: ver 0.1, library management, offline luc install -->

<p align="center">
  <img src="luc-vscode/icons/luc.svg" width="120" alt="LUC Logo"/>
</p>

<p align="center">
  A fast, lightweight scripting language Ã¢â‚¬â€ easy like Lua, with more features built in.
</p>

<p align="center">
  <img src="https://img.shields.io/badge/version-0.1-orange" />
  <img src="https://img.shields.io/badge/platform-Windows-blue" />
  <img src="https://img.shields.io/badge/license-Apache%202.0-green" />
  <img src="https://img.shields.io/badge/built%20with-C99-lightgrey" />
</p>

---

##  Why LUC?

| | LUC | Python | Lua |
|--|--|--|--|
| Speed (1M loop) | 19ms Ã¢Å“â€¦ | 263ms Ã¢ÂÅ’ | 15ms Ã¢Å“â€¦ |
| Easy to learn | Ã¢Å“â€¦ | Ã¢Å“â€¦ | Ã¢Å“â€¦ |
| Window / GUI | Ã¢Å“â€¦ | Ã¢Å“â€¦ | Ã¢ÂÅ’ |
| JSON built-in | Ã¢Å“â€¦ | Ã¢Å“â€¦ | Ã¢ÂÅ’ |
| Binary size | ~2000KB Ã¢Å“â€¦ | ~30MB Ã¢ÂÅ’ | ~200KB Ã¢Å“â€¦ |
| Task / coroutine | Ã¢Å“â€¦ | Ã¢ÂÅ’ | Ã¢Å“â€¦ |

LUC is **3x faster than Python** on simple loops, **lightweight under 1MB**, and has all the features Lua is missing.

---

##  Install

**Windows** Ã¢â‚¬â€ download and run:

**[luc-installer.exe](https://github.com/hsusulist/luc/releases/latest)**

Double click Ã¢â€ â€™ choose components Ã¢â€ â€™ Install Ã¢â€ â€™ Done.  
Open any terminal and type `luc --version` to verify.

### What gets installed

The Components page keeps the install light by default:

| Component | Default | What it is |
|---|---|---|
| **main** | Ã¢Å“â€ always | LUC interpreter + core libraries (string, list, math, bit32, JSON, buffer, IO, OS, task, coroutine) |
| **window** | Ã¢ËœÂ opt-in | SDL2 2D graphics, PNG/JPG sprites, TTF text, WAV/OGG/MP3 sound + demos |
| **ailib** | Ã¢ËœÂ opt-in | lanternl AI library Ã¢â‚¬â€ `import ai` |
| **vsext** | Ã¢Å“â€ | VS Code extension (syntax highlighting) |
| **source** | Ã¢Å“â€ | C source code for developers |

Skipped `window`/`ailib` and changed your mind later? No installer needed Ã¢â‚¬â€
see [`luc install`](#luc-install--package-manager) below.

### Re-running the installer (maintenance)

Run `luc-installer.exe` again Ã¢â‚¬â€ or open **Luc Installer** straight from the
Start Menu Ã¢â‚¬â€ and it detects the existing install, then offers three choices:

| Option | What it does |
|---|---|
| **Install libraries** | opens the components page pre-ticked with your last selection: tick to add a library, **untick to remove it** |
| **Fix** | repair: silently reinstall all program files over the current folder, settings kept |
| **Uninstall** | remove LUC completely |

---

## luc install Ã¢â‚¬â€ package manager

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

## Ã°Å¸ÂªÅ¸ Window / GUI

`window` lÃƒÂ  **thÃ†Â° viÃ¡Â»â€¡n hÃ¡Â»â€¡ thÃ¡Â»â€˜ng** cÃ¡Â»Â§a LUC Ã¢â‚¬â€ nÃ¡ÂºÂ¡p bÃ¡ÂºÂ±ng `import` (khÃƒÂ´ng phÃ¡ÂºÂ£i `require`).
CÃ¡ÂºÂ§n bÃ¡ÂºÂ£n window: `luc install window` (SDL2 + font + sound).

CÃƒÂ¡ch dÃ¡Â»â€¦ nhÃ¡ÂºÂ¥t cho ngÃ†Â°Ã¡Â»Âi mÃ¡Â»â€ºi Ã¢â‚¬â€ `window.go()` lo hÃ¡ÂºÂ¿t vÃƒÂ²ng lÃ¡ÂºÂ·p, bÃ¡ÂºÂ¡n chÃ¡Â»â€° viÃ¡ÂºÂ¿t code vÃ¡ÂºÂ½:

```lua
import window("w")

create x, vx = 100, 200

w.go("Game Cua To", 800, 600, function(dt)
    x = x + vx * dt
    if x > 780 or x < 20 then vx = -vx end

    w.clear("navy")
    w.circle(x, 300, 30, "yellow")
    w.text_center("Xin chÃƒÂ o LUC! CÃƒÂ³ dÃ¡ÂºÂ¥u tiÃ¡ÂºÂ¿ng ViÃ¡Â»â€¡t.", 100, "white", 32)
end)
```

MuÃ¡Â»â€˜n tÃ¡Â»Â± Ã„â€˜iÃ¡Â»Âu khiÃ¡Â»Æ’n vÃƒÂ²ng lÃ¡ÂºÂ·p (nhÃ†Â° demo Pong) thÃƒÂ¬ dÃƒÂ¹ng `start / running / update / close`
cÃ…Â©ng Ã„â€˜Ã†Â°Ã¡Â»Â£c Ã¢â‚¬â€ cÃ¡ÂºÂ£ hai cÃƒÂ¡ch Ã„â€˜Ã¡Â»Âu chÃ¡ÂºÂ¡y.

```lua
-- hÃƒÂ¬nh Ã¡ÂºÂ£nh: PNG/JPG/GIF (AVIF/JPEG-XL khÃƒÂ´ng ship kÃƒÂ¨m)
create hero = w.sprite("hero.png")
w.draw(hero, x, y)
w.draw(hero, x, y, { "scale": 2, "rotate": 45, "flip": "x", "alpha": 128, "center": true })

-- chÃ¡Â»Â¯: font TTF mÃ¡ÂºÂ·c Ã„â€˜Ã¡Â»â€¹nh gÃƒÂµ Ã„â€˜Ã†Â°Ã¡Â»Â£c tiÃ¡ÂºÂ¿ng ViÃ¡Â»â€¡t; thiÃ¡ÂºÂ¿u mÃƒÂ u thÃƒÂ¬ lÃ¡ÂºÂ¥y trÃ¡ÂºÂ¯ng
w.text("Ã„ÂiÃ¡Â»Æ’m: 10", 10, 10, "white", 24)
w.text_center("TÃ¡ÂºÂ M DÃ¡Â»ÂªNG", 200, "gold", 40)
w.font("myfont.ttf", 20)   -- font riÃƒÂªng (tÃƒÂ¹y chÃ¡Â»Ân)

-- ÃƒÂ¢m thanh: WAV luÃƒÂ´n chÃ¡ÂºÂ¡y; OGG/MP3/FLAC chÃ¡ÂºÂ¡y khi Ã„â€˜Ã¡Â»Â§ DLL Ã„â€˜i kÃƒÂ¨m
create jump = w.sound("jump.wav")
w.play(jump)
w.play(jump, { "loop": 2, "volume": 80 })
create bgm = w.music("nhac.ogg")
w.play_music(bgm)          -- lÃ¡ÂºÂ·p vÃƒÂ´ hÃ¡ÂºÂ¡n mÃ¡ÂºÂ·c Ã„â€˜Ã¡Â»â€¹nh
w.stop_music()

-- phÃƒÂ­m/chuÃ¡Â»â„¢t: w.key("space"), w.key_pressed("escape"), w.mouse() ...
-- xem thÃƒÂªm: demos/beginner.luc (dÃ¡Â»â€¦) vÃƒÂ  demos/pong.luc (Ã„â€˜Ã¡Â»Â§ tÃƒÂ­nh nÃ„Æ’ng)
```

> **import vs require** Ã¢â‚¬â€ `import` chÃ¡Â»â€° nÃ¡ÂºÂ¡p thÃ†Â° viÃ¡Â»â€¡n hÃ¡Â»â€¡ thÃ¡Â»â€˜ng (`window`, `ai`, `json`).
> Module cÃ¡Â»Â§a bÃƒÂªn thÃ¡Â»Â© ba nÃ¡ÂºÂ¡p bÃ¡ÂºÂ±ng `require`: `create mylib = require("mylib")` Ã¢â‚¬â€
> vÃƒÂ  `require("window")` sÃ¡ÂºÂ½ luÃƒÂ´n lÃ¡ÂºÂ¥y module `window` cÃ¡Â»Â§a bÃ¡ÂºÂ¡n (nÃ¡ÂºÂ¿u cÃƒÂ³), khÃƒÂ´ng phÃ¡ÂºÂ£i cÃ¡Â»Â§a hÃ¡Â»â€¡ thÃ¡Â»â€˜ng.

---

##  AI library (lanternl)

lanternl Ã¢â‚¬â€ a mini-PyTorch written in pure Lua Ã¢â‚¬â€ is ported to LUC and ships
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
| `import window` | GUI windows, drawing, input (`import window("w")` Ã„â€˜Ã¡Â»Æ’ Ã„â€˜Ã¡ÂºÂ·t tÃƒÂªn ngÃ¡ÂºÂ¯n) |

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
Ã¢â€Å“Ã¢â€â‚¬Ã¢â€â‚¬ build_installer.ps1  Ã¢â€ Â builds dist\luc-installer.exe (Windows)
Ã¢â€Å“Ã¢â€â‚¬Ã¢â€â‚¬ src/                 Ã¢â€ Â luc_core.c + luc_libs.c (C99)
Ã¢â€Å“Ã¢â€â‚¬Ã¢â€â‚¬ luc_modules/         Ã¢â€ Â lanternl AI library ported to LUC (import ai)
Ã¢â€Å“Ã¢â€â‚¬Ã¢â€â‚¬ packages/            Ã¢â€ Â ai.lucpkg bundle consumed by "luc install ai"
Ã¢â€Å“Ã¢â€â‚¬Ã¢â€â‚¬ tools/               Ã¢â€ Â make_pkg.ps1 (rebuild the ai bundle)
Ã¢â€Å“Ã¢â€â‚¬Ã¢â€â‚¬ demos/               Ã¢â€ Â example .luc files (hello, json, pong, minesweeper)
Ã¢â€Å“Ã¢â€â‚¬Ã¢â€â‚¬ vscode/              Ã¢â€ Â VS Code extension (syntax highlighting + icons)
Ã¢â€Å“Ã¢â€â‚¬Ã¢â€â‚¬ installer/           Ã¢â€ Â Inno Setup script + assets
Ã¢â€Å“Ã¢â€â‚¬Ã¢â€â‚¬ dist/                Ã¢â€ Â compiled binaries + luc-installer.exe
Ã¢â€Å“Ã¢â€â‚¬Ã¢â€â‚¬ minesweeper.luc      Ã¢â€ Â minesweeper game (window lib demo)
Ã¢â€Å“Ã¢â€â‚¬Ã¢â€â‚¬ LICENSE
Ã¢â€â€Ã¢â€â‚¬Ã¢â€â‚¬ README.md
```

---

##  License

Apache License 2.0 Ã¢â‚¬â€ see [LICENSE](LICENSE)

---

<p align="center">Made with Ã¢ÂÂ¤Ã¯Â¸Â by <a href="https://github.com/hsusulist">hsusulist</a></p>

