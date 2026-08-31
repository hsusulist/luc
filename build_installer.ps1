# LUC - build 2 exe (day du + core only) + Inno Setup installer
# Yeu cau: MSYS2 MinGW64 (gcc + SDL2), Inno Setup 6/7
$ErrorActionPreference = 'Stop'

$env:Path += ';C:\msys64\mingw64\bin'
$env:Path += ';C:\Program Files\Inno Setup 7'
$env:Path += ';C:\Program Files (x86)\Inno Setup 7'
$env:Path += ';C:\Program Files\Inno Setup 6'
$env:Path += ';C:\Program Files (x86)\Inno Setup 6'

$ProjectDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$AppDir = Join-Path $ProjectDir 'dist\app'
$InstallerScript = Join-Path $ProjectDir 'installer\luc-installer.iss'
$MingwBin = 'C:\msys64\mingw64\bin'

if (Test-Path $AppDir) {
    Remove-Item -Recurse -Force $AppDir
}
New-Item -ItemType Directory -Force -Path $AppDir | Out-Null

$gcc = Get-Command gcc -ErrorAction SilentlyContinue
if (-not $gcc) {
    throw 'MinGW gcc was not found in PATH. Open MSYS2 MinGW64 shell or add mingw64\bin to PATH.'
}

$CoreSrc = Join-Path $ProjectDir 'src\luc_core.c'
$LibsSrc = Join-Path $ProjectDir 'src\luc_libs.c'

# Build 1: ban DAY DU (moi lib + window, kem SDL2.dll) - danh cho choi game/window
Write-Host 'Building luc.exe (full)...'
& gcc -O2 -s -std=c99 -DLUC_WINDOW -DLUC_NO_TTF -DLUC_NO_IMAGE -mwindows `
    -o (Join-Path $AppDir 'luc.exe') `
    $CoreSrc $LibsSrc `
    -lm -lSDL2
if ($LASTEXITCODE -ne 0) {
    throw 'Build full failed. Check SDL2 headers/libs and MinGW installation.'
}

# Build 2: ban CORE ONLY (console, khong window, khong can SDL2.dll) - hoc script/chenh nhinh
Write-Host 'Building luc-core.exe (core only)...'
& gcc -O2 -s -std=c99 `
    -o (Join-Path $AppDir 'luc-core.exe') `
    $CoreSrc $LibsSrc `
    -lm
if ($LASTEXITCODE -ne 0) {
    throw 'Build core-only failed.'
}

# SDL2.dll chi can cho ban day du
foreach ($dll in @('SDL2.dll')) {
    $src = Join-Path $MingwBin $dll
    if (Test-Path $src) {
        Copy-Item $src $AppDir
    } else {
        Write-Warning "$dll not found in $MingwBin - installer may fail to find it."
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
