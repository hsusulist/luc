@echo off
rem ============================================================
rem  rebuild.bat - recompile luc.exe from the bundled C source.
rem  Asks whether to include the window/SDL2 libraries or to
rem  build the plain console version.
rem
rem  Tries every gcc in PATH (plus C:\msys64\mingw64\bin) and
rem  uses the first one that can actually compile AND run a
rem  test program, so a broken MinGW install (cc1.exe missing
rem  its runtime DLLs) is skipped instead of failing halfway.
rem
rem  When lucgcc.exe sits next to this script it is used to run
rem  the compiler, so a broken toolchain fails SILENTLY (it sets
rem  SEM_FAILCRITICALERRORS for the whole child chain) instead of
rem  popping "System Error" dialogs per missing DLL.
rem
rem  If no working gcc: install MSYS2 (https://msys2.org),
rem  then in a MinGW64 shell:  pacman -S mingw-w64-x86_64-gcc
rem ============================================================
setlocal EnableExtensions
cd /d "%~dp0"

if not exist src\luc_core.c (
  echo Source code not found in src\.
  echo Re-run the installer and tick "C source and build files".
  exit /b 1
)

rem PRE = silent-fail runner (already quoted), empty when unavailable
set "PRE="
if exist "%~dp0lucgcc.exe" set PRE="%~dp0lucgcc.exe"

set "GCC="
for /f "delims=" %%G in ('where gcc 2^>nul') do (
  if not defined GCC call :trygcc "%%~G"
)
if not defined GCC if exist "C:\msys64\mingw64\bin\gcc.exe" call :trygcc "C:\msys64\mingw64\bin\gcc.exe"

if not defined GCC (
  echo No WORKING gcc was found.
  echo A broken MinGW install ^(cc1.exe missing libgmp-10.dll, libisl-23.dll,
  echo libmpc-3.dll or libmpfr-6.dll^) also counts as "not found" here.
  echo.
  echo Fix: install MSYS2 from msys2.org, then in a MinGW64 shell run:
  echo   pacman -S mingw-w64-x86_64-gcc
  exit /b 1
)

echo Using gcc: %GCC%
echo.
choice /C YN /M "Include window/SDL2 support"
if errorlevel 2 goto console

if not exist sdl2\include\SDL2\SDL.h (
  echo sdl2\ not found - re-run the installer with "Window support"
  echo ticked, or answer N to build the console version.
  exit /b 1
)

echo Building luc.exe with window/SDL2 support ...
%PRE% "%GCC%" -O2 -s -std=c99 src\luc_core.c src\luc_libs.c -DLUC_WINDOW -DLUC_NO_TTF -DLUC_NO_IMAGE -I"%~dp0sdl2\include" -L"%~dp0sdl2\lib" -o luc.exe -lm -lmingw32 -lSDL2main -lSDL2 -mwindows
if errorlevel 1 (
  echo Standard SDL2 link failed - trying the minimal link instead ...
  %PRE% "%GCC%" -O2 -s -std=c99 src\luc_core.c src\luc_libs.c -DLUC_WINDOW -DLUC_NO_TTF -DLUC_NO_IMAGE -I"%~dp0sdl2\include" -L"%~dp0sdl2\lib" -o luc.exe -lm -lSDL2 -mwindows
  if errorlevel 1 (
    echo Build FAILED. Check the gcc messages above.
    exit /b 1
  )
)
if exist sdl2\bin\SDL2.dll copy /y sdl2\bin\SDL2.dll SDL2.dll >nul
echo Done - luc.exe rebuilt successfully.
exit /b 0

:console
echo Building console luc.exe ...
%PRE% "%GCC%" -O2 -s -std=c99 src\luc_core.c src\luc_libs.c -o luc.exe -lm
if errorlevel 1 (
  echo Build FAILED. Check the gcc messages above.
  exit /b 1
)
echo Done - luc.exe rebuilt successfully.
exit /b 0

rem ---- probe one gcc candidate: must compile AND run a tiny program ----
:trygcc
set "PROBEBASE=%TEMP%\luc_probe_%RANDOM%"
> "%PROBEBASE%.c" echo int main^(void^){return 0;}
%PRE% "%~1" -O2 -std=c99 "%PROBEBASE%.c" -o "%PROBEBASE%.exe" >nul 2>&1
if errorlevel 1 goto trygcc_broken
if not exist "%PROBEBASE%.exe" goto trygcc_broken
%PRE% "%PROBEBASE%.exe" >nul 2>&1
if errorlevel 1 (
  del /q "%PROBEBASE%.c" "%PROBEBASE%.exe" >nul 2>&1
  echo [skip] gcc compiles but its exe will not run: %~1
  exit /b 0
)
del /q "%PROBEBASE%.c" "%PROBEBASE%.exe" >nul 2>&1
set "GCC=%~1"
exit /b 0

:trygcc_broken
del /q "%PROBEBASE%.c" "%PROBEBASE%.exe" >nul 2>&1
echo [skip] broken gcc ^(missing runtime DLLs?^): %~1
exit /b 0
