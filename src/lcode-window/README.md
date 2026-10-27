# LCode

LCode's editor source is organized by responsibility:

| Folder | Files |
| --- | --- |
| `editor/` | Buffer editing, rendering, and input |
| `explorer/` | File tree, menus, file operations, and dialogs |
| `features/` | Settings, search, plugins, suggestions, and terminal |
| `language/` | Syntax highlighting, language detection, and icons |

`lcode.c` is the core and includes the modules in build order. Compile it by
including `lcode-window/lcode.c` from the LUC core translation unit; do not compile
the feature files separately. This keeps LCode's internal helpers private and
preserves the existing LUC runtime integration.

## Build and run

Build LUC using the repository's normal Windows build command. `luc --lcode [path]`
opens the editor; the release also provides `lcode.exe` as an editor launcher.
LCode uses Windows console APIs.

## Use in another LUC checkout

Copy this directory to `src\lcode-window\` in the other checkout and include
`lcode-window/lcode.c` from its core translation unit. Build and run that checkout
normally. The C source is a component of the LUC runtime, not a standalone
source-only editor.

For an installable editor-only build, install `@hsusulist/lcode`; it downloads
the standalone LCode release without installing the LUC command.
