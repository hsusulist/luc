# @hsusulist/luc

Install and launch the [LUC](https://github.com/hsusulist/luc) language on Windows.

```powershell
npm install -g @hsusulist/luc
luc --version
```

The package installs the runtime into `%LOCALAPPDATA%\Programs\LUC` (no PATH
changes, no admin rights). If the automatic install is skipped, run:

```powershell
luc --install
```

`luc --install` opens the terminal installer (license, components, folder and a
Continue button). Unattended installs:

```powershell
luc --install -y --no-path
luc --install -c window,ai -d "D:\Tools\LUC" -y
```

Available components: `window`, `ai`, `discord`, `lcode`, `vscode`, `source`.
The core interpreter is always installed.

The npm package is a small launcher; the runtime is downloaded from the signed
GitHub release and verified with SHA-256 and Ed25519 (minisign format) before it
is installed.
