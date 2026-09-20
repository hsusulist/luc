# LUC 0.1 - build the Inno Setup installer (PREBUILT binaries)
# update 2026-09-01: -lwinhttp for both builds, no installer\sdl2 fallback
#
# The installer ships READY-TO-RUN exes + SDL2.dll. The USER'S PC needs
# nothing at all: no compiler, no MSYS2, no downloads during setup -
# just like installing Python. This script only runs on YOUR build
# machine and does two things:
#
#   1) Collect the binaries into dist\app:
#        luc-win.exe    - full build with SDL2 window support
#        luc-core.exe   - console-only build
#        SDL2.dll       - SDL2 runtime that ships next to luc.exe
#      If the prebuilt trio shipped in this zip is present it is
#      SMOKE-TESTED and reused as-is (the smoke test really starts
#      the exe, so a broken/missing SDL2.dll is caught here on your
#      machine, not on the user's). Only when files are missing or
#      fail the test are they rebuilt - which requires MinGW gcc
#      (MSYS2 MinGW64 shell) and the SDL2 dev package
#      (pacman -S mingw-w64-x86_64-SDL2).
#
#   2) Run ISCC to pack dist\luc-installer.exe.
#
# Yeu cau: Inno Setup 6/7. gcc chi can khi phai build lai.
$ErrorActionPreference = 'Stop'

$env:Path += ';C:\msys64\mingw64\bin'
$env:Path += ';C:\Program Files\Inno Setup 7'
$env:Path += ';C:\Program Files (x86)\Inno Setup 7'
$env:Path += ';C:\Program Files\Inno Setup 6'
$env:Path += ';C:\Program Files (x86)\Inno Setup 6'

$ProjectDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$AppDir = Join-Path $ProjectDir 'dist\app'
$InstallerScript = Join-Path $ProjectDir 'installer\luc-installer.iss'
$SrcCore = Join-Path $ProjectDir 'src\luc_core.c'
$SrcLibs = Join-Path $ProjectDir 'src\luc_libs.c'
$SrcTrans = Join-Path $ProjectDir 'src\luc_trans.c'

# luc.exe dang chay se khoa file -> tat het truoc khi lam viec
Get-Process luc, luc-core, luc-win, luc-fallback -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Milliseconds 300

New-Item -ItemType Directory -Force -Path $AppDir | Out-Null

function Test-LucExe {
    # Smoke test: the exe must start AND load SDL2.dll (where present).
    # A missing/unblocked-failed DLL gives a huge negative exit code.
    param([string]$Exe)
    & $Exe -e 'print(1)' | Out-Null
    return ($LASTEXITCODE -eq 0)
}

function Get-Sdl2Layout {
    # Returns $true and fills $SDLInc/$SDLLib/$SDLDll when the MSYS2 SDL2
    # dev package exists (the only SDL2 source: the MSYS2 dev package
    # and the old v1/v2 helpers were removed from the repo)
    $cands = @(
        @{ Inc = 'C:\msys64\mingw64\include'; Lib = 'C:\msys64\mingw64\lib'; Dll = 'C:\msys64\mingw64\bin\SDL2.dll' }
    )
    foreach ($c in $cands) {
        if ((Test-Path (Join-Path $c.Inc 'SDL2\SDL.h')) -and (Test-Path $c.Lib) -and (Test-Path $c.Dll)) {
            return $true, $c.Inc, $c.Lib, $c.Dll
        }
    }
    return $false, $null, $null, $null
}

function Build-Console {
    param([string]$Gcc)
    Write-Host 'Building luc-core.exe (console)...'
    # -lwinhttp: HTTP client used by the built-in 'luc install' command
    & $Gcc -O2 -s -std=gnu99 -static-libgcc -o (Join-Path $AppDir 'luc-core.exe') $SrcCore $SrcLibs $SrcTrans -lm -lwinhttp -lws2_32 -lsecur32
    if ($LASTEXITCODE -ne 0) { throw 'Build luc-core.exe failed.' }
}

function Build-Window {
    param([string]$Gcc)
    $ok, $sdlInc, $sdlLib, $sdlDll = Get-Sdl2Layout
    if (-not $ok) {
        throw @'
SDL2 dev files not found. Install them in an MSYS2 MinGW64 shell:
  pacman -S mingw-w64-x86_64-SDL2 mingw-w64-x86_64-SDL2_ttf mingw-w64-x86_64-SDL2_mixer
'@
    }
    Write-Host 'Building luc-win.exe (window: SDL2 + TTF/image/mixer via runtime binding)...'
    # NOTE: no -lSDL2_ttf/-lSDL2_image/-lSDL2_mixer here on purpose: the satellite
    # DLLs bind at RUNTIME (LoadLibrary), so the exe starts without them.
    # -lwinhttp: HTTP client used by the built-in 'luc install' command
    # NOTE: console subsystem on purpose (IMAGE_SUBSYSTEM_WINDOWS_CUI = 3;
    # GUI is 2). Never -mwindows: a GUI-subsystem exe detaches from the
    # console at launch, so the shell prints the next prompt immediately
    # and our output arrives late (or never) -- this broke `luc`, `luc
    # build` and `luc install` for anyone installing the window component
    # (the installer maps luc-win.exe to luc.exe). -lSDL2main drags in a
    # WinMain entry that flips the linker to GUI, so force console twice:
    # -mconsole plus a trailing -Wl,--subsystem,console (last flag wins).
    # SDL2 windows work fine from a console exe; a console window simply
    # stays behind GUI apps instead.
    & $Gcc -O2 -s -std=gnu99 -static-libgcc -mconsole `
        -o (Join-Path $AppDir 'luc-win.exe') $SrcCore $SrcLibs $SrcTrans `
        -DLUC_WINDOW `
        "-I$sdlInc" "-L$sdlLib" -lm -lmingw32 -lSDL2main -lSDL2 -lwinhttp -lws2_32 -lsecur32 "-Wl,--subsystem,console"
    if ($LASTEXITCODE -ne 0) { throw 'Build luc-win.exe failed.' }
    Copy-Item $sdlDll (Join-Path $AppDir 'SDL2.dll') -Force
    # minimal SDL2_image/SDL2_mixer (PNG/JPG/GIF + WAV/MP3/OGG/FLAC/OPUS, ~2MB
    # instead of ~30MB of AV1/JPEG-XL/MOD codecs) built from source:
    powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $ProjectDir 'tools\build_sdl_min.ps1')
    if ($LASTEXITCODE -ne 0) { throw 'Build of minimal SDL2_image/SDL2_mixer failed.' }
    # stock SDL2_ttf + runtime closure of every satellite DLL + default font:
    $satBin = 'C:\msys64\mingw64\bin'
    $satDlls = @('SDL2_ttf.dll','libfreetype-6.dll','libharfbuzz-0.dll','libbz2-1.dll',
        'libpng16-16.dll','zlib1.dll','libbrotlidec.dll','libbrotlicommon.dll',
        'libgraphite2.dll','libglib-2.0-0.dll','libintl-8.dll','libpcre2-8-0.dll',
        'libiconv-2.dll','libjpeg-8.dll','libmpg123-0.dll','libogg-0.dll',
        'libopus-0.dll','libopusfile-0.dll','libvorbis-0.dll','libvorbisfile-3.dll',
        'libFLAC.dll','libgcc_s_seh-1.dll','libstdc++-6.dll','libwinpthread-1.dll')
    foreach ($d in $satDlls) {
        $src = Join-Path $satBin $d
        if (-not (Test-Path $src)) { throw "satellite DLL missing: $src" }
        Copy-Item $src (Join-Path $AppDir $d) -Force
    }
    $fontSrc = 'C:\msys64\mingw64\share\fonts\TTF\DejaVuSans.ttf'
    if (-not (Test-Path $fontSrc)) { throw "default font missing: $fontSrc (pacman -S mingw-w64-x86_64-ttf-dejavu)" }
    Copy-Item $fontSrc (Join-Path $AppDir 'DejaVuSans.ttf') -Force
}

$winExe = Join-Path $AppDir 'luc-win.exe'
$coreExe = Join-Path $AppDir 'luc-core.exe'
$dll = Join-Path $AppDir 'SDL2.dll'

$needBuild = $false

if ((Test-Path $coreExe) -and (Test-LucExe $coreExe)) {
    Write-Host 'Prebuilt luc-core.exe: smoke test PASSED (reused as-is).'
} else {
    Write-Warning 'luc-core.exe missing or smoke test FAILED -> will rebuild.'
    $needBuild = $true
}

if ((Test-Path $winExe) -and (Test-Path $dll) -and (Test-LucExe $winExe)) {
    Write-Host 'Prebuilt luc-win.exe + SDL2.dll: smoke test PASSED (reused as-is).'
} else {
    Write-Warning 'luc-win.exe/SDL2.dll missing or smoke test FAILED -> will rebuild.'
    $needBuild = $true
}

if ($needBuild) {
    $gcc = Get-Command gcc -ErrorAction SilentlyContinue
    if (-not $gcc) {
        throw @'
gcc was not found in PATH and the prebuilt exes are unusable. Either restore
dist\app\ (luc-win.exe, luc-core.exe, SDL2.dll from the zip) or install MinGW
gcc: MSYS2 MinGW64 shell -> pacman -S mingw-w64-x86_64-gcc
'@
    }
    if (-not ((Test-Path $coreExe) -and (Test-LucExe $coreExe))) {
        Build-Console $gcc.Path
        if (-not (Test-LucExe $coreExe)) { throw 'Rebuilt luc-core.exe failed its smoke test.' }
    }
    if (-not ((Test-Path $winExe) -and (Test-Path $dll) -and (Test-LucExe $winExe))) {
        Build-Window $gcc.Path
        if (-not (Test-LucExe $winExe)) { throw 'Rebuilt luc-win.exe failed its smoke test (is SDL2.dll next to it?).' }
    }
}

# pack packages\ai.lucpkg so "luc install ai" also works offline
if (Test-Path (Join-Path $ProjectDir 'luc_modules')) {
    Write-Host 'Packing packages\ai.lucpkg (offline source for luc install ai)...'
    & (Join-Path $ProjectDir 'tools\make_pkg.ps1')
}

$iscc = Get-Command iscc -ErrorAction SilentlyContinue
if (-not $iscc) {
    $isccCandidates = @(
        'C:\Program Files\Inno Setup 7\ISCC.exe',
        'C:\Program Files (x86)\Inno Setup 7\ISCC.exe',
        'C:\Program Files\Inno Setup 6\ISCC.exe',
        'C:\Program Files (x86)\Inno Setup 6\ISCC.exe'
    )
    $foundIscc = $isccCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
    if (-not $foundIscc) {
        throw 'Inno Setup (ISCC.exe) was not found.'
    }
    $iscc = @{ Source = $foundIscc }
}

& $iscc.Source $InstallerScript
if ($LASTEXITCODE -ne 0) {
    throw 'Installer build failed.'
}

Write-Host 'Installer created successfully: dist\luc-installer.exe'
Write-Host 'Welcome page should say "LUC 0.1" - if not, the old .iss is being used.'
