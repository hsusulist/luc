# @hsusulist/luc

Install and launch the [LUC](https://github.com/hsusulist/luc) language on Windows or Linux.

```powershell
npm install -g @hsusulist/luc
luc --version
```

On Linux, the package compiles the runtime during installation. Install a C
compiler such as GCC or Clang first; set `CC` if your compiler is not named
`cc`. Run `luc install` to rebuild the native runtime.

On Windows, the package installs the runtime into
`%LOCALAPPDATA%\Programs\LUC` (no PATH changes, no admin rights). If the
automatic install is skipped, run:

```powershell
luc install
```

`luc install` opens the terminal installer (license, components, folder and a
Continue button). Unattended installs:

```powershell
luc install -y --no-path
luc install -c window,ai -d "D:\Tools\LUC" -y
```

Available components: `window`, `ai`, `discord`, `lcode`, `vscode`, `source`.
LCode is optional and is not selected by default. Selecting it installs
`lcode.exe` alongside the interpreter. To install only the editor without the
LUC command, use `npm install -g @hsusulist/lcode`.

On Windows, the npm package is a small launcher; the runtime is downloaded from
the signed GitHub release and verified with SHA-256 and Ed25519 (minisign
format) before it is installed. On Linux, the package builds from its bundled
LUC and LCode C sources.

The editor-only package uses the separate, signed `lcode-portable.zip` release
asset on Windows. On Linux it compiles the included native terminal editor.

## Registry

```powershell
luc login
luc whoami
luc install -p @username/packagename
luc publish -p
```

`luc publish -p` uploads the `.luic` file and optional `README.md` from the
current folder, then opens a browser to enter the package name and summary.
Login state is scoped to the current terminal session. Set `LUC_REGISTRY_URL`
to use a different registry (defaults to `http://localhost:3000`).
