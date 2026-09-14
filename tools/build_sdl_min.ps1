# LUC - build minimal SDL2_image + SDL2_mixer DLLs (PNG/JPG/GIF + WAV/MP3/OGG/FLAC/OPUS)
#
# Stock MSYS2 satellite DLLs drag in ~30MB of AV1/JPEG-XL/TIFF/MOD codecs
# through load-time linking. These minimal builds need only:
#   image: libpng, libjpeg, zlib          (~1MB + SDL2)
#   mixer: libmpg123, libvorbisfile, libFLAC, libopusfile (+ogg/opus/vorbis)
# TTF stays stock (freetype/harfbuzz are small).
# Run from the repo root:  powershell -File tools\build_sdl_min.ps1
# Output: dist\app\SDL2_image.dll + SDL2_mixer.dll
$ErrorActionPreference = 'Stop'

$env:Path += ';C:\msys64\mingw64\bin'
$gcc = (Get-Command gcc -ErrorAction SilentlyContinue).Path
if (-not $gcc) { throw 'gcc not found (MSYS2 MinGW64 shell: pacman -S mingw-w64-x86_64-gcc)' }

$ProjectDir = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$WorkDir = Join-Path $env:TEMP 'luc_sdlmin'
$AppDir = Join-Path $ProjectDir 'dist\app'
New-Item -ItemType Directory -Force -Path $WorkDir | Out-Null
New-Item -ItemType Directory -Force -Path $AppDir | Out-Null

$ImgVer = '2.8.8'
$MixVer = '2.8.1'
$ImgTgz = Join-Path $WorkDir "SDL2_image-$ImgVer.tar.gz"
$MixTgz = Join-Path $WorkDir "SDL2_mixer-$MixVer.tar.gz"
if (-not (Test-Path $ImgTgz)) {
    Write-Host "downloading SDL2_image $ImgVer..."
    Invoke-WebRequest -Uri "https://github.com/libsdl-org/SDL_image/releases/download/release-$ImgVer/SDL2_image-$ImgVer.tar.gz" -OutFile $ImgTgz
}
if (-not (Test-Path $MixTgz)) {
    Write-Host "downloading SDL2_mixer $MixVer..."
    Invoke-WebRequest -Uri "https://github.com/libsdl-org/SDL_mixer/releases/download/release-$MixVer/SDL2_mixer-$MixVer.tar.gz" -OutFile $MixTgz
}
Push-Location $WorkDir
# NOTE: --exclude Xcode = macOS-only framework symlinks that bsdtar cannot
# create on Windows; without it tar exits nonzero and (under 'Stop')
# aborts the whole script even though every needed source extracted fine.
& tar -xzf $ImgTgz --exclude='*/Xcode/*'
& tar -xzf $MixTgz --exclude='*/Xcode/*'
if (-not (Test-Path "SDL2_image-$ImgVer/src/IMG.c")) { throw 'SDL2_image sources failed to extract' }
if (-not (Test-Path "SDL2_mixer-$MixVer/src/mixer.c")) { throw 'SDL2_mixer sources failed to extract' }

# stubs for formats LUC does not ship (AVIF/JPEG-XL/TIFF/WebP)
$stubs = Join-Path $ProjectDir 'tools\img_stubs.c'
if (-not (Test-Path $stubs)) { throw 'tools\img_stubs.c missing' }

$Inc = @('C:\msys64\mingw64\include', 'C:\msys64\mingw64\include\SDL2', 'C:\msys64\mingw64\include\opus')
$Lib = 'C:\msys64\mingw64\lib'
$I = $Inc | ForEach-Object { '-I' + $_ }

Write-Host 'building SDL2_image.dll (PNG/JPG/GIF/BMP + stb formats)...'
$imgSrc = @('IMG.c','IMG_bmp.c','IMG_gif.c','IMG_jpg.c','IMG_lbm.c','IMG_pcx.c','IMG_png.c','IMG_pnm.c','IMG_qoi.c','IMG_stb.c','IMG_svg.c','IMG_tga.c','IMG_xcf.c','IMG_xpm.c','IMG_xv.c') |
    ForEach-Object { "SDL2_image-$ImgVer/src/$_" }
& $gcc -O2 -shared -o (Join-Path $AppDir 'SDL2_image.dll') @imgSrc $stubs `
    -DLOAD_BMP -DLOAD_GIF -DLOAD_JPG -DLOAD_PNG @I "-L$Lib" `
    -lSDL2 -lpng -ljpeg -lz
if ($LASTEXITCODE -ne 0) { throw 'SDL2_image build failed' }

Write-Host 'building SDL2_mixer.dll (WAV/MP3/OGG/FLAC/OPUS)...'
$mixSrc = @('mixer.c','music.c','utils.c','effects_internal.c','effect_position.c','effect_stereoreverse.c') |
    ForEach-Object { "SDL2_mixer-$MixVer/src/$_" }
$mixCod = @('load_aiff.c','load_voc.c','music_wav.c','music_mpg123.c','mp3utils.c','music_ogg.c','music_flac.c','music_opus.c') |
    ForEach-Object { "SDL2_mixer-$MixVer/src/codecs/$_" }
& $gcc -O2 -shared -o (Join-Path $AppDir 'SDL2_mixer.dll') @mixSrc @mixCod `
    -DMUSIC_WAV -DMUSIC_MP3_MPG123 -DMUSIC_OGG -DMUSIC_FLAC_LIBFLAC -DMUSIC_OPUS @I `
    "-ISDL2_mixer-$MixVer/src" "-ISDL2_mixer-$MixVer/src/codecs" "-L$Lib" `
    -lSDL2 -lmpg123 -lvorbisfile -lFLAC -lopusfile
if ($LASTEXITCODE -ne 0) { throw 'SDL2_mixer build failed' }

Pop-Location
Write-Host 'minimal SDL DLLs -> dist\app (SDL2_image.dll, SDL2_mixer.dll)'
