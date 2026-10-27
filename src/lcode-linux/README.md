# LCode Linux backend

This directory is selected by `src/luc_core.c` for non-Windows builds. It is
the first POSIX terminal backend and currently provides file loading, editing,
and saving with raw terminal input.

This is an initial port, not feature parity with the Windows editor yet; the
Linux build currently does not use the copied Windows explorer, search,
settings, plugin, or integrated-terminal modules.

From the repository root in Linux, build and run it with:

```sh
gcc -O2 -std=gnu99 -o luc src/luc_core.c src/luc_libs.c src/luc_trans.c -lm -ldl
./luc --lcode path/to/file.luc
```

Use the arrow keys to move, Enter to insert a line break, Backspace to delete,
Ctrl+S to save, and Ctrl+Q to save and quit. When no path is supplied, Ctrl+S
creates `untitled.luc` in the current directory.

The Windows implementation remains in `src/lcode-window/`. Linux-specific
terminal, filesystem, and editor features can be developed here without
changing the Windows implementation.
