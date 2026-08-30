# LUC Programming Language

<p align="center">
  <img src="luc-vscode/icons/luc.svg" width="120" alt="LUC Logo"/>
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

## ⚡ Why LUC?

| | LUC | Python | Lua |
|--|--|--|--|
| Speed (1M loop) | 19ms ✅ | 263ms ❌ | 15ms ✅ |
| Easy to learn | ✅ | ✅ | ✅ |
| Window / GUI | ✅ | ✅ | ❌ |
| JSON built-in | ✅ | ✅ | ❌ |
| Binary size | ~500KB ✅ | ~30MB ❌ | ~200KB ✅ |
| Task / coroutine | ✅ | ❌ | ✅ |

LUC is **3x faster than Python** on simple loops, **lightweight under 1MB**, and has all the features Lua is missing.

---

## 🚀 Install

**Windows** — download and run:

👉 **[luc-installer.exe](https://github.com/hsusulist/luc/releases/latest)**

Double click → Next → Install → Done.  
Open any terminal and type `luc --version` to verify.

---

## 📝 Quick Start

Create a file `hello.luc`:

```lua
print("Hello from LUC!")

-- list (Python style)
local fruits = ["apple", "banana", "orange"]
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

```lua
local window = require("window")

window.start("My App", 800, 600)

while window.running() do
    window.clear("black")
    window.circle(400, 300, 50, "red")
    window.text("Hello LUC!", 10, 10, "white")
    window.fps(60)
    window.update()
end

window.close()
```

---

## 📦 Built-in Libraries

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
| `require("window")` | GUI windows, drawing, input |

---

## 🔤 Syntax

LUC syntax is based on Lua 5.1 — all 21 keywords supported.  
Extra features on top:

```lua
-- Type annotations (optional, zero cost)
local x: number = 10
local name: string = "LUC"

-- List (Python style, 1-indexed like Lua)
local arr = [1, 2, 3]
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
local json = require("json")
local t = json.decode('{"x":1}')
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

## 🛠️ Build from Source

**Requirements:** gcc (MinGW on Windows), SDL2 (optional, for window library)

```bash
# Basic (no window)
gcc -O2 -std=c99 -o luc src/main.c -lm

# With window support (Windows MinGW)
pacman -S mingw-w64-x86_64-SDL2 mingw-w64-x86_64-SDL2_ttf mingw-w64-x86_64-SDL2_image
gcc -O2 -std=c99 -DLUC_WINDOW -o luc src/main.c -lm -lSDL2 -lSDL2_ttf -lSDL2_image

# With window support (Linux)
sudo apt install libsdl2-dev libsdl2-ttf-dev libsdl2-image-dev
gcc -O2 -std=c99 -DLUC_WINDOW -o luc src/main.c -lm $(pkg-config --libs sdl2 SDL2_ttf SDL2_image)
```

---

## 📁 Project Structure

```
luc/
├── luc-installer.exe   ← installer for Windows users
├── src/
│   └── main.c          ← full LUC implementation (single file, C99)
├── luc-vscode/         ← VSCode extension (syntax highlighting + icon)
├── installer/          ← Inno Setup scripts
├── dist/app/           ← compiled binaries
├── examples/           ← example .luc files
├── LICENSE
└── README.md
```

---

## 📄 License

Apache License 2.0 — see [LICENSE](LICENSE)

---

<p align="center">Made with ❤️ by <a href="https://github.com/hsusulist">hsusulist</a></p>