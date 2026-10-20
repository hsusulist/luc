
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

### Update, rollback and verification

The installer extracts into a staging folder and only then moves files into
place. Every replaced file is renamed to a temporary backup first; if anything
fails, the backup is restored and the previous version is left intact. Running
LUC processes and locked files are detected before the install starts. Selected
files are replaced in place, so your scripts and other unmanaged files in the
install folder are preserved.

Archives are limited to 4096 entries, 2 GB uncompressed and a 200:1 compression
ratio. Paths with `..`, drive letters, Alternate Data Streams (`:`), reserved
Windows device names (`CON`, `NUL`, `COM1`...) and trailing dots/spaces are
rejected.

Downloads are verified in this order:

1. SHA-256 from `luc-portable.zip.sha256`.
2. Ed25519 signature of the checksum file (`luc-portable.zip.sha256.minisig`,
   minisign legacy format) against the public key embedded in `install.ps1`.
3. GitHub's own asset digest, when available.

Local archives require a signature unless `-AllowUnsigned` is passed explicitly.
Releases without a signature still install with a warning for compatibility;
publish `luc-portable.zip.sha256.minisig` with `tools/lucsig.js` to make the
signature mandatory.

The public key lives in `installer/minisign.pub`; private keys are never stored
in the repository.

