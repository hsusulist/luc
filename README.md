
<!-- update 2026-09-21: ver 0.2-beta1, discord embeds/buttons -->
# LUC
<p align="center">
  <img src="vscode/icons/luc.svg" width="120" alt="LUC Logo"/>
</p>

<p align="center">
  A fast, lightweight scripting language — easy like Lua, with more features built in.
</p>

<p align="center">
  <img src="https://img.shields.io/badge/version-0.2--beta1-orange" />
  <img src="https://img.shields.io/badge/platform-Windows%20%7C%20Linux-blue" />
  <img src="https://img.shields.io/badge/license-Apache%202.0-green" />
  <img src="https://img.shields.io/badge/built%20with-C99-lightgrey" />
</p>

---


##  Build on Linux

```sh
# Debian/Ubuntu deps (OpenSSL for https/wss; SDL2 only for `import window`)
sudo apt install build-essential libssl-dev
sudo apt install libsdl2-dev libsdl2-ttf-dev libsdl2-image-dev libsdl2-mixer-dev   # optional

make                 # console build: core + net (https via OpenSSL) + lcode editor
make WINDOW=1        # also build the SDL2 window library
make TLS=0           # no OpenSSL (https/wss disabled)
make test
sudo make install    # /usr/local/bin/luc + /usr/local/share/luc/luc_modules
```

Linux notes: `luc build` (AOT .exe) and the x86-64 JIT are Windows-only for now,
so scripts run on the bytecode VM. lcode config lives in `~/.config/LUC/lcode.cfg`;
the clipboard uses wl-copy / xclip / xsel when installed.

##  About
- Luc is a programing languange thats acually easy


- Luc Copyright by Duy 2026-2026


- Luc is a free software, released under Apache Liscense

<p align="center">Made with ❤️ by <a href="https://github.com/hsusulist">hsusulist</a></p>

