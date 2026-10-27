# @hsusulist/lcode

Install LCode by itself on Windows or Linux:

```powershell
npm install -g @hsusulist/lcode
lcode path/to/file.luc
```

On Linux, the package compiles the included terminal editor during installation.
Install GCC or Clang first (or set `CC` to your compiler). `lcode --install`
rebuilds it.

The package downloads and verifies the dedicated `lcode-portable.zip` release
asset and installs it to `%LOCALAPPDATA%\Programs\LCode` on Windows. It does
not install the `luc` command. Each Windows GitHub release needs this ZIP, its
`.sha256` checksum, and `.sha256.minisig` signature attached.
To install or update the editor later, run:

```powershell
lcode --install
```

To install the interpreter too, install `@hsusulist/luc`. Run `luc --install`
to choose **LCode support** in its component chooser, or use both npm packages.
