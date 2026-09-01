LUC - BUILDING THE WINDOWS INSTALLER (setup v3, prebuilt)
=========================================================

What changed in v3
------------------
The installer now ships READY-TO-RUN binaries:

  dist\app\luc-win.exe    full build with SDL2 window support
  dist\app\luc-core.exe   console-only build
  dist\app\SDL2.dll       SDL2 runtime, installed next to luc.exe

They were cross-compiled with mingw-w64 GCC 14 and tested (interpreter
test suite + real SDL2 window rendering). The user's PC needs NOTHING:
no compiler, no MSYS2, no MinGW, no downloads during setup - tick the
components, click Install, done. Just like installing Python.

The old setup-time compile (v1/v2: gcc probing, lucgcc.exe silent-fail
helper, rebuild.bat, install-time SDL2 headers) is completely gone,
together with its failure modes (missing-DLL "System Error" dialogs,
"No working C compiler was found", rebuild.bat problems).

Also fixed: the Finish-page shortcut "Open the LUC installation folder"
used to call CreateProcess() on the FOLDER itself, which fails with
"CreateProcess failed; code 5. Access is denied.". It now uses the
shellexec flag so Explorer opens the folder.

How to build dist\luc-installer.exe
-----------------------------------
1. Copy this luc\ folder over your build folder (keep your existing
   installer\ assets: LICENSE.txt, luc.ico, lucdownload2.bmp,
   lucdownload.bmp - they are machine-local and not in this zip).
2. Right-click build_installer.ps1 -> Run with PowerShell.

   The script smoke-tests the shipped prebuilt exes by really starting
   them (luc-core.exe -e "print(1)"). If they pass, it reuses them
   as-is - gcc is NOT needed. Only when files are missing or fail the
   test does it rebuild them, which requires:
     - MinGW gcc      (MSYS2 MinGW64: pacman -S mingw-w64-x86_64-gcc)
     - SDL2 dev files (MSYS2 MinGW64: pacman -S mingw-w64-x86_64-SDL2)
3. Inno Setup 6/7 (ISCC.exe) packs dist\luc-installer.exe.

Verify you are running the new installer: the Welcome page must say
"Setup v3: everything is prebuilt - no compiler, no MSYS2 ...".
AppVersion is 0.1.2.

Component layout (Components page)
----------------------------------
  main    (fixed)  LUC interpreter - prebuilt, runs immediately
  window           SDL2 window support + Pong demo - prebuilt
  ailib            lanternl AI library ('import ai')
  vsext            VS Code extension
  source           keep the C source in the install folder

If "window" is ticked, luc.exe IS the SDL2 build (it still works as a
normal console interpreter - it attaches to your terminal). Untick it
and the console-only build is installed as luc.exe instead.
