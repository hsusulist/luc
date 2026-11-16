<p align="center">
  <img src="vscode/icons/luc.svg" width="120" alt="LUC Logo"/>
</p>

<h1 align="center">LUC</h1>

<p align="center">
  A fast, lightweight scripting language. Easy like Lua, with more built in.
</p>

<p align="center">
  <img src="https://img.shields.io/badge/version-0.2.0--beta--1-orange" />
  <img src="https://img.shields.io/badge/platform-Windows%20%7C%20Linux-blue" />
  <img src="https://img.shields.io/badge/license-Apache%202.0-green" />
  <img src="https://img.shields.io/badge/built%20with-C99-lightgrey" />
</p>

---

## What is LUC

LUC is a small scripting language written in C99. It runs on a register-based VM with an x86-64 JIT on Windows. The syntax is Lua-like but cleaner, and the standard library covers things you usually need extra packages for: JSON, HTTP/WebSocket, big-integer math, statistics, matrices, graphics windows, and Discord bots.

## Quick look

```luc
create name = "world"                -- 'create' declares a local variable

create function greet(who)
    return "hello " + who            -- '+' joins strings
end
print(greet(name))                   -- hello world

create xs = [10, 20, 30]             -- list, 0-based
xs:append(40)
repeat v, i in xs do                 -- the only loop keyword
    print(i, v)
end

create cfg = {"width": 800, "height": 600}   -- dict
print(cfg.width, cfg:keys())
```

## Key differences from Lua

| Lua | LUC |
|---|---|
| `local x = 1` | `create x = 1` (a bare `x = 1` is a global) |
| `for` / `while ... until` | `repeat ... do ... end` and `while ... do ... end` |
| `#x` | `len(x)` |
| `~=` | `!=` |
| `pairs` / `ipairs` | `repeat k, v in t do` |
| lists start at 1 | lists start at 0 (dicts keep 1-based integer keys) |

Using Lua-only syntax gives an error that tells you the LUC replacement. Add `!strict` on line 1 of a file to require declared variables.

## Install

### Windows

Download and run [luc-installer.exe](https://github.com/hsusulist/luc/releases/latest/download/luc-installer.exe). It uses a per-user folder, needs no admin rights, and bundles everything required.

Or use PowerShell:

```powershell
Invoke-WebRequest https://raw.githubusercontent.com/hsusulist/luc/main/install.ps1 -OutFile .\install.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File .\install.ps1
```

### npm (Windows and Linux)

```sh
npm install -g @hsusulist/luc
luc --version
```

On Windows the package is a small launcher that installs the runtime into `%LOCALAPPDATA%\Programs\LUC` and does not touch PATH. On Linux it compiles the runtime during install, so GCC or Clang is required.

### Components and unattended install

The core interpreter is always installed. Optional components: `window`, `ai`, `discord`, `lcode`, `vscode`, `source`.

```powershell
luc install                              # install or repair LUC itself
luc install -c window,ai -d "D:\Tools\LUC" -y
luc install -p                           # pick a package from a menu, Enter installs it
luc install -p ai discord                # install packages directly
```

Packages added with `luc install -p` are always downloaded from GitHub (no offline copy); without internet the command says so and changes nothing. Reopening `luc-installer.exe` offers Modify, Repair, Reinstall and Uninstall.

Flags: `-y`, `-c <list>`, `-d <dir>`, `--no-path`, `--no-env`, `--force`, `--quiet`, `--archive <zip>`, `--source github|local`.

## Command line

| Command | Action |
|---|---|
| `luc script.luc [args]` | run a script (args available as `...`) |
| `luc -e "print(1 + 2)"` | run a snippet |
| `luc` | open the interactive help desk (type a keyword such as `repeat` or `net`) |
| `luc --libs` | list core, installed and user libraries |
| `luc install -p [name]` / `luc remove <name>` | add (from GitHub) or remove an optional library |
| `luc lcode [path]` | open the LCode editor |
| `luc build in.luc -o out.exe` | compile to a standalone executable (Windows x64 only) |
| `luc uninstall` | uninstall LUC |
| `-v`, `-h` | version, help |

## Libraries

Built in, no install needed: `string`, `list`, `table`, `math`, `bit32`, `os`, `io`, `buffer`, `coroutine`, `task`, `json`, `net`.

Load with `import`:

| Import | Provides |
|---|---|
| `import nums` | random, search, time, matrix, sets, finance and extra list helpers |
| `import net("n")` | TCP, HTTP/HTTPS, WebSocket, servers |
| `import json` | `json.encode`, `json.decode` |
| `import libs` | `command`, `make`, `get`, `pack` for writing your own libraries |
| `import window("w")` | SDL2 graphics, input, sound (needs `window` component) |
| `import ai` | tokenizer, MLP and text-model training (needs `ai` component) |
| `import discord("bot")` | Discord bots (needs `discord` component) |

Third-party `.luc` files load with `create m = require("name")`. Extra search folders go in `LUC_PATH`.

## Examples

HTTP request:

```luc
import net("n")
create body, status = n.get("https://example.com")
print(status)
```

Window:

```luc
import window("w")
w.go("Hello LUC", 800, 600, function(dt)
    w.clear("navy")
    w.circle(400, 300, 30, "yellow")
end)
```

Discord bot:

```luc
import discord("bot")
bot:run(bot.token("TOKEN_HERE"))
while bot.run do
    if bot.prefix("!") == "ping" then bot.msg:send("pong!") end
end
```

More in `demos/` (libs, net, window, ai, discord).

## Editor support

- VS Code: copy `vscode/` to `~/.vscode/extensions/luc` for highlighting, snippets and Lua-syntax warnings.
- LCode: built-in terminal editor, run `luc lcode`.

## Platform notes

| Feature | Windows | Linux |
|---|---|---|
| Interpreter and core libraries | yes | yes |
| JIT and `luc build` | yes | no |
| HTTPS / WSS | yes | no (plain HTTP and TCP only) |
| `window` (SDL2) | yes | no |
| LCode editor | full | basic terminal editing |

## Documentation

- `Help.txt`: full language and library reference.
- `luc --help` and the `luc` help desk: built-in docs for every keyword and library.
- `luc_modules/README.txt`: notes on the `ai` module.

## License

Apache License 2.0. Copyright 2026 Duy.

Made by [hsusulist](https://github.com/hsusulist).