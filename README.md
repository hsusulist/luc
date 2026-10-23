
<!-- update 2026-09-21: ver 0.2.0-beta-1, discord embeds/buttons -->
# LUC
<p align="center">
  <img src="vscode/icons/luc.svg" width="120" alt="LUC Logo"/>
</p>

<p align="center">
  A fast, lightweight scripting language — easy like Lua, with more features built in.
</p>

<p align="center">
  <img src="https://img.shields.io/badge/version-0.2.0--beta--1-orange" />
  <img src="https://img.shields.io/badge/platform-Windows-blue" />
  <img src="https://img.shields.io/badge/license-Apache%202.0-green" />
  <img src="https://img.shields.io/badge/built%20with-C99-lightgrey" />
</p>

---


##  About
- Luc is a programing languange thats acually easy


- Luc Copyright by Duy 2026-2026


- Luc is a free software, released under Apache Liscense

<p align="center">Made with ❤️ by <a href="https://github.com/hsusulist">hsusulist</a></p>

## Install (Windows)

### Terminal installer

Download the setup script, then run it in PowerShell:

```powershell
Invoke-WebRequest https://raw.githubusercontent.com/hsusulist/luc/main/install.ps1 -OutFile .\install.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File .\install.ps1
```

`luc --install` opens the same installer once LUC is installed. The interactive
UI shows the Apache 2.0 license, a component selector, the install folder and a
**Continue** button (English). Running `luc` with no arguments also opens it on
the first run, then starts the REPL afterwards.

Unattended installs skip every prompt:

```powershell
luc --install -y --no-path
luc --install -c window,ai -d "D:\Tools\LUC" -y
```

Components: `window`, `ai`, `discord`, `lcode`, `vscode`, `source` (the core
interpreter is always installed). Useful flags: `--no-path`, `--no-env`,
`--force`, `--quiet`, `--archive <zip>`, `--source github|local`.

### npm

```powershell
npm install -g @hsusulist/luc
luc --version
```

The npm package is a small launcher. It installs the runtime into
`%LOCALAPPDATA%\Programs\LUC` and does not modify PATH.

