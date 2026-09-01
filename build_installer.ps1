# LUC - build the Inno Setup installer (PREBUILT binaries, setup v3)
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
    # Returns $true and fills $SDLInc/$SDLLib/$SDLDll when SDL2 dev files exist
    $cands = @(
        @{ Inc = Join-Path $ProjectDir 'installer\sdl2\include'; Lib = Join-Path $ProjectDir 'installer\sdl2\lib'; Dll = Join-Path $ProjectDir 'installer\sdl2\bin\SDL2.dll' },
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
    & $Gcc -O2 -s -std=c99 -static-libgcc -o (Join-Path $AppDir 'luc-core.exe') $SrcCore $SrcLibs -lm
    if ($LASTEXITCODE -ne 0) { throw 'Build luc-core.exe failed.' }
}

function Build-Window {
    param([string]$Gcc)
    $ok, $sdlInc, $sdlLib, $sdlDll = Get-Sdl2Layout
    if (-not $ok) {
        throw @'
SDL2 dev files not found. Either:
  - pacman -S mingw-w64-x86_64-SDL2 in an MSYS2 MinGW64 shell, or
  - put the SDL2 mingw dev package in installer\sdl2\ (include\, lib\, bin\SDL2.dll)
'@
    }
    Write-Host 'Building luc-win.exe (window, SDL2)...'
    & $Gcc -O2 -s -std=c99 -static-libgcc `
        -o (Join-Path $AppDir 'luc-win.exe') $SrcCore $SrcLibs `
        -DLUC_WINDOW -DLUC_NO_TTF -DLUC_NO_IMAGE `
        "-I$sdlInc" "-L$sdlLib" -lm -lmingw32 -lSDL2main -lSDL2 -mwindows
    if ($LASTEXITCODE -ne 0) { throw 'Build luc-win.exe failed.' }
    Copy-Item $sdlDll (Join-Path $AppDir 'SDL2.dll') -Force
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
Write-Host 'Welcome page should say "Setup v3" - if not, the old .iss is being used.'
