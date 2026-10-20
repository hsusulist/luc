
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

## Install without the GUI installer (Windows)

Download the setup script, then run it in PowerShell:

```powershell
Invoke-WebRequest https://raw.githubusercontent.com/hsusulist/luc/main/install.ps1 -OutFile .\install.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File .\install.ps1
```

The setup prints the LUC banner and latest version, then prompts for optional
components and an install folder. It downloads `luc-portable.zip` and its
SHA-256 file from the latest GitHub Release, verifies the archive, and installs
the selected files. The release must include both files; the normal release build
creates them with `powershell -ExecutionPolicy Bypass -File .\tools\make_portable.ps1`
(the full `build_installer.ps1` creates the portable ZIP and GUI installer).
