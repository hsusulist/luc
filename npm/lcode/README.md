# @hsusulist/lcode

Install LCode by itself on Windows:

```powershell
npm install -g @hsusulist/lcode
lcode .
```

The package downloads and verifies the dedicated `lcode-portable.zip` release
asset and installs it to `%LOCALAPPDATA%\Programs\LCode`. It does not install
the `luc` command. Each GitHub release needs this ZIP, its `.sha256` checksum,
and `.sha256.minisig` signature attached.
To install or update the editor later, run:

```powershell
lcode --install
```

To install the interpreter too, install `@hsusulist/luc`. Run `luc --install`
to choose **LCode support** in its component chooser, or use both npm packages.
