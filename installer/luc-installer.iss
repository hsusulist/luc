; LUC - Inno Setup script (PREBUILT, English, setup v3)
; update 2026-09-01: AppId, opt-in window/ai, maintenance dialog, start menu
; update 2026-09-02: ver 0.1, library management, offline packages
; update 2026-09-21: ver 0.2.0-beta-1
; Setup v3 changes EVERYTHING about how window support works:
; the installer now ships READY-TO-RUN binaries built ahead of
; time on the developer's machine (build_installer.ps1):
;   luc.exe     - full build with SDL2 window support (when the
;                 "window" component is ticked) + SDL2.dll
;   luc-core.exe - console-only build (installed as luc.exe
;                 when "window" is unticked)
; The user's PC needs NOTHING: no compiler, no MSYS2, no MinGW,
; no downloads during setup. Ticking "Install" and clicking Next
; is enough - exactly like installing Python. The old setup-time
; compile (v1/v2: probing gcc, lucgcc.exe silent-fail helper,
; rebuild.bat, install-time SDL2 headers) is GONE.
; The old Finish-page bug is fixed as well: "Open the LUC
; installation folder" used to call CreateProcess() on the
; FOLDER itself (-> "CreateProcess failed; code 5. Access is
; denied."). It now uses the shellexec flag so Explorer opens
; the folder instead.
; Pages: Language -> Welcome -> License -> Components ->
;        Install folder -> Ready -> Installing -> Finish
; update 2026-11-16: tidy components tree, Modify / Repair / Reinstall
; Maintenance: re-running this setup (or the "Luc Installer" Start Menu
; entry) compares versions first. A different installed version means a
; plain upgrade over the old folder (welcome/license/folder pages skipped,
; previous components pre-ticked).
; The same version shows a dialog (radio buttons + Continue), like Python's:
;   Modify     - straight to the components page: tick to add, untick to
;                remove; Next installs immediately (no Ready page)
;   Repair     - no pages at all: re-lays every file of the installed
;                components, re-downloads the script libraries, re-registers
;                PATH / LUC_PATH / file associations, cleans leftovers
;                (luc.exe.old, *.tmp, *.part, packages\) and runs
;                "luc --version" to verify - the Finish page shows a report
;   Reinstall  - no pages at all: deletes LUC's own files first, then
;                installs this version again (same folder + components);
;                user plugins, user modules and settings are kept
;   Uninstall  - remove LUC completely
; Where the files come from:
;   first install / upgrade -> this setup's payload (works offline); no
;                              offline copy is kept afterwards ({app}\packages
;                              from older setups is deleted)
;   Modify / Repair / Reinstall -> the script libraries (ai, discord) are
;                              downloaded from GitHub with "luc install -p";
;                              binaries (luc.exe, lcode.exe, window DLLs) still
;                              come from the payload so they always match
;                              this setup's version
; Outside the installer: "luc install -p [ai|discord|window]" (always GitHub).
; "net" and "http" need no component: they come with the interpreter.
; Build the installer: run build_installer.ps1, or
;   ISCC installer\luc-installer.iss
;   (needs dist\app\luc-win.exe, dist\app\luc-core.exe,
;    dist\app\SDL2.dll - shipped prebuilt in this zip)
; Required files in installer\: LICENSE.txt, luc.ico,
;   lucdownload2.bmp, lucdownload.bmp

; File lists used by [Code] (keep in sync with luc_remove_system_library
; in luc_core.c)
#define AiFiles "ai.luc,bench_gpu_transformer.luc,blas.luc,data.luc,embedding.luc,forge.luc,gpu.luc,gpu_transformer.luc,lmio.luc,lmlog.luc,lmtrain.luc,lmtrain_gpu.luc,luaTL.luc,luatl_adapter.luc,luaTL_train.luc,matrix.luc,nn.luc,optim.luc,optim2.luc,positional.luc,rope.luc,tensor.luc,tensor2.luc,tokenizer.luc,train.luc,transformer.luc,README.txt"
#define WindowFiles "SDL2.dll,SDL2_ttf.dll,SDL2_image.dll,SDL2_mixer.dll,DejaVuSans.ttf,libfreetype-6.dll,libharfbuzz-0.dll,libbz2-1.dll,libpng16-16.dll,zlib1.dll,libbrotlidec.dll,libbrotlicommon.dll,libgraphite2.dll,libglib-2.0-0.dll,libintl-8.dll,libpcre2-8-0.dll,libiconv-2.dll,libjpeg-8.dll,libmpg123-0.dll,libogg-0.dll,libopus-0.dll,libopusfile-0.dll,libvorbis-0.dll,libvorbisfile-3.dll,libFLAC.dll,libgcc_s_seh-1.dll,libstdc++-6.dll,libwinpthread-1.dll"

[Setup]
; AppId pins the uninstall registry key (Luc_is1). Same value the AppName
; fallback produced before, so installs made without this line stay detected.
AppId=Luc
AppName=Luc
AppVersion=0.2.0-beta-1
AppPublisher=hsusulist
DefaultDirName={userpf}\LUC
DefaultGroupName=LUC
OutputBaseFilename=luc-installer
OutputDir=..\dist
Compression=lzma2
SolidCompression=yes
LicenseFile=LICENSE.txt
PrivilegesRequired=none
SetupIconFile=luc.ico
VersionInfoProductName=Luc
VersionInfoDescription=LUC programming language installer
VersionInfoCompany=hsusulist
WizardImageFile=lucdownload2.bmp
WizardSmallImageFile=lucdownload.bmp
WizardImageStretch=yes
WizardStyle=modern
UninstallDisplayIcon={app}\luc.exe
ChangesEnvironment=yes
; Locked files ("another program is using luc") instead of cryptic errors:
; the Restart Manager asks to close the locking apps, then restarts them.
CloseApplications=yes
RestartApplications=yes
DisableProgramGroupPage=yes
DisableDirPage=no
; Inno 7 HIDES the Welcome page by default (shDisableWelcomePage is in the
; compiler's default option set) - show it again, it carries the "Setup v3"
; marker the user can verify before installing
DisableWelcomePage=no
; auto: only asked when Windows' language has no matching translation
ShowLanguageDialog=auto

[Languages]
Name: "en"; MessagesFile: "compiler:Default.isl"
#if FileExists(AddBackslash(CompilerPath) + "Languages\Vietnamese.isl")
Name: "vi"; MessagesFile: "compiler:Languages\Vietnamese.isl"
#endif

[Types]
; one type only: the "type" drop-down is hidden, the tree below is all there is
Name: "custom"; Description: "Custom installation"; Flags: iscustom

[Components]
; Short names, grouped. Libraries are opt-in; add them later any time with
; "luc install -p <name>" or by reopening this setup (Modify).
; Old flat names (window, ailib, discord, vsext, lcode, lcodefont) from
; earlier setups are mapped in NormComps() below.
Name: "main"; Description: "LUC interpreter + core libraries (net, http, json, io, os, task...)"; Types: custom; Flags: fixed
Name: "libs"; Description: "Libraries"
Name: "libs\window"; Description: "window - 2D graphics, sprites, text and sound (SDL2)"
Name: "libs\ai"; Description: "ai - lanternl neural networks, training, tokenizer"
Name: "libs\discord"; Description: "discord - Discord bot library"
Name: "tools"; Description: "Tools"
Name: "tools\vscode"; Description: "VS Code syntax highlighting"; Types: custom
Name: "tools\lcode"; Description: "LCode editor"
Name: "tools\lcode\font"; Description: "Nerd Font for LCode file icons"
Name: "source"; Description: "C source code (for developers)"; Types: custom

[Files]
; Full build with SDL2 window support, installed as luc.exe
Source: "..\dist\app\luc-win.exe"; DestDir: "{app}"; DestName: "luc.exe"; Components: libs\window; Flags: ignoreversion
; SDL2 runtime that goes with it (loaded from the install folder)
Source: "..\dist\app\SDL2.dll"; DestDir: "{app}"; Components: libs\window; Flags: ignoreversion
; Satellite media DLLs (runtime-bound: TTF text, PNG/JPG images, WAV/OGG/MP3 sound)
Source: "..\dist\app\SDL2_ttf.dll"; DestDir: "{app}"; Components: libs\window; Flags: ignoreversion
Source: "..\dist\app\SDL2_image.dll"; DestDir: "{app}"; Components: libs\window; Flags: ignoreversion
Source: "..\dist\app\SDL2_mixer.dll"; DestDir: "{app}"; Components: libs\window; Flags: ignoreversion
Source: "..\dist\app\libfreetype-6.dll"; DestDir: "{app}"; Components: libs\window; Flags: ignoreversion
Source: "..\dist\app\libharfbuzz-0.dll"; DestDir: "{app}"; Components: libs\window; Flags: ignoreversion
Source: "..\dist\app\libbz2-1.dll"; DestDir: "{app}"; Components: libs\window; Flags: ignoreversion
Source: "..\dist\app\libpng16-16.dll"; DestDir: "{app}"; Components: libs\window; Flags: ignoreversion
Source: "..\dist\app\zlib1.dll"; DestDir: "{app}"; Components: libs\window; Flags: ignoreversion
Source: "..\dist\app\libbrotlidec.dll"; DestDir: "{app}"; Components: libs\window; Flags: ignoreversion
Source: "..\dist\app\libbrotlicommon.dll"; DestDir: "{app}"; Components: libs\window; Flags: ignoreversion
Source: "..\dist\app\libgraphite2.dll"; DestDir: "{app}"; Components: libs\window; Flags: ignoreversion
Source: "..\dist\app\libglib-2.0-0.dll"; DestDir: "{app}"; Components: libs\window; Flags: ignoreversion
Source: "..\dist\app\libintl-8.dll"; DestDir: "{app}"; Components: libs\window; Flags: ignoreversion
Source: "..\dist\app\libpcre2-8-0.dll"; DestDir: "{app}"; Components: libs\window; Flags: ignoreversion
Source: "..\dist\app\libiconv-2.dll"; DestDir: "{app}"; Components: libs\window; Flags: ignoreversion
Source: "..\dist\app\libjpeg-8.dll"; DestDir: "{app}"; Components: libs\window; Flags: ignoreversion
Source: "..\dist\app\libmpg123-0.dll"; DestDir: "{app}"; Components: libs\window; Flags: ignoreversion
Source: "..\dist\app\libogg-0.dll"; DestDir: "{app}"; Components: libs\window; Flags: ignoreversion
Source: "..\dist\app\libopus-0.dll"; DestDir: "{app}"; Components: libs\window; Flags: ignoreversion
Source: "..\dist\app\libopusfile-0.dll"; DestDir: "{app}"; Components: libs\window; Flags: ignoreversion
Source: "..\dist\app\libvorbis-0.dll"; DestDir: "{app}"; Components: libs\window; Flags: ignoreversion
Source: "..\dist\app\libvorbisfile-3.dll"; DestDir: "{app}"; Components: libs\window; Flags: ignoreversion
Source: "..\dist\app\libFLAC.dll"; DestDir: "{app}"; Components: libs\window; Flags: ignoreversion
Source: "..\dist\app\libgcc_s_seh-1.dll"; DestDir: "{app}"; Components: libs\window; Flags: ignoreversion
Source: "..\dist\app\libstdc++-6.dll"; DestDir: "{app}"; Components: libs\window; Flags: ignoreversion
Source: "..\dist\app\libwinpthread-1.dll"; DestDir: "{app}"; Components: libs\window; Flags: ignoreversion
; Default TTF font (Vietnamese-capable) used when the user picks no font
Source: "..\dist\app\DejaVuSans.ttf"; DestDir: "{app}"; Components: libs\window; Flags: ignoreversion
; Console-only build, installed as luc.exe when "window" is unticked
Source: "..\dist\app\luc-core.exe"; DestDir: "{app}"; DestName: "luc.exe"; Check: ConsoleSelected; Flags: ignoreversion
; Optional LCode command; the core build is also used when luc.exe has window support.
Source: "..\dist\app\luc-win.exe"; DestDir: "{app}"; DestName: "lcode.exe"; Components: tools\lcode and libs\window; Flags: ignoreversion
Source: "..\dist\app\luc-core.exe"; DestDir: "{app}"; DestName: "lcode.exe"; Components: tools\lcode; Check: ConsoleSelected; Flags: ignoreversion
; Terminal installer used by "luc install" and for updating portable builds
Source: "..\install.ps1"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\installer\minisign.pub"; DestDir: "{app}"; Flags: ignoreversion
; Runnable example scripts (window, net, discord, ai demos in subfolders)
Source: "..\demos\*"; DestDir: "{app}\demos"; Flags: ignoreversion recursesubdirs createallsubdirs
; This setup itself, so users can reopen the maintenance dialog
; (Modify / Repair / Reinstall / Uninstall) from the Start Menu.
; external: {srcexe} resolves at run time, not compile time
Source: "{srcexe}"; DestDir: "{app}"; DestName: "luc-installer.exe"; Flags: external ignoreversion; Check: NotSelfCopy
; http client/server module: part of the core, always installed
Source: "..\luc_modules\http.luc"; DestDir: "{app}\luc_modules"; Flags: ignoreversion skipifsourcedoesntexist
; lanternl AI library ('import ai') and Discord bot library ('import discord').
; FromPayload: laid from this setup only on a first install / upgrade.
; In Modify / Repair / Reinstall they are downloaded from GitHub instead
; ("luc install -p ai|discord", see FetchLibraries). No offline copy is kept.
Source: "..\luc_modules\*"; DestDir: "{app}\luc_modules"; Components: libs\ai; Flags: ignoreversion recursesubdirs createallsubdirs; Excludes: "discord*.luc,http.luc"; Check: FromPayload
Source: "..\luc_modules\discord.luc"; DestDir: "{app}\luc_modules"; Components: libs\discord; Flags: ignoreversion; Check: FromPayload
; FiraCode Nerd Font for lcode file icons (registered with Windows, kept on uninstall)
Source: "..\fonts\FiraCodeNerdFontMono-Regular.ttf"; DestDir: "{fonts}"; FontInstall: "FiraCode Nerd Font Mono"; Flags: skipifsourcedoesntexist uninsneveruninstall sharedfile; Components: tools\lcode\font
; LCode plugins (built-in luc example: suggestions, syntax colors, themes)
Source: "..\plugin\*"; DestDir: "{app}\plugin"; Components: tools\lcode; Flags: ignoreversion recursesubdirs createallsubdirs skipifsourcedoesntexist
; VS Code extension (copied into the VS Code extensions folder)
Source: "..\vscode\*"; DestDir: "{%USERPROFILE}\.vscode\extensions\hsusulist.luc"; Components: tools\vscode; Flags: ignoreversion recursesubdirs createallsubdirs
; C source for developers (explicit: src also contains stray exes that
; must NOT be shipped - they would silently bloat the installer)
Source: "..\src\*.h"; DestDir: "{app}\src"; Components: source; Flags: ignoreversion
Source: "..\src\*.c"; DestDir: "{app}\src"; Components: source; Flags: ignoreversion
Source: "..\src\lcode-window\*"; DestDir: "{app}\src\lcode"; Components: source; Flags: ignoreversion recursesubdirs createallsubdirs

[InstallDelete]
; offline package copies from older setups ("luc install -p" never uses them)
Type: filesandordirs; Name: "{app}\packages"

[Icons]
Name: "{group}\LUC"; Filename: "{app}\luc.exe"
Name: "{group}\LCode"; Filename: "{app}\lcode.exe"; Parameters: """{app}\demos"""; Components: tools\lcode
Name: "{userdesktop}\LUC"; Filename: "{app}\luc.exe"
; Reopens the maintenance dialog (Modify / Repair / Reinstall / Uninstall) any time
Name: "{group}\Luc Installer"; Filename: "{app}\luc-installer.exe"

[Registry]
Root: HKCU; Subkey: "Environment"; ValueType: expandsz; ValueName: "Path"; ValueData: "{olddata};{app}"; Check: AddAppDirToPath
; LUC_PATH is set unconditionally: "luc install -p ai" must be able to drop
; modules into luc_modules even when the ai library was not ticked
Root: HKCU; Subkey: "Environment"; ValueType: expandsz; ValueName: "LUC_PATH"; ValueData: "{app}\luc_modules"; Flags: uninsdeletevalue
; Keep normal file-open defaults untouched; these entries add LUC to Open with.
Root: HKCU; Subkey: "Software\Classes\.luc\OpenWithProgids"; ValueType: string; ValueName: "LUC.Library"; ValueData: ""; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\Classes\.luic\OpenWithProgids"; ValueType: string; ValueName: "LUC.Library"; ValueData: ""; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\Classes\LUC.Library"; ValueType: string; ValueName: ""; ValueData: "LUC library"; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\LUC.Library\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\luc.exe"" ""%1"""
Root: HKCU; Subkey: "Software\Classes\.cfg\OpenWithProgids"; ValueType: string; ValueName: "LUC.PluginConfig"; ValueData: ""; Components: tools\lcode; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\Classes\LUC.PluginConfig"; ValueType: string; ValueName: ""; ValueData: "LCode plugin configuration"; Components: tools\lcode; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\LUC.PluginConfig\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\lcode.exe"" --lcode ""%1"""; Components: tools\lcode
; Snapshot of the ticked components: Modify / Repair / Reinstall and
; upgrades start from (and Repair restores) the last selection
Root: HKCU; Subkey: "Software\LUC"; ValueType: string; ValueName: "Components"; ValueData: "{code:ComponentSnapshot}"; Flags: uninsdeletekey

[Run]
; shellexec is REQUIRED here: CreateProcess() cannot open a folder
; (that was the "CreateProcess failed; code 5. Access is denied."
; error on the v1/v2 Finish page). With shellexec, Explorer opens it.
Filename: "{app}"; Description: "Open the LUC installation folder"; Flags: postinstall shellexec skipifsilent unchecked

[UninstallDelete]
; VS Code extension was installed outside {app}
Type: filesandordirs; Name: "{%USERPROFILE}\.vscode\extensions\hsusulist.luc"
; Leftovers the uninstaller does not track: exe backups from
; "luc install -p window" swaps (luc.exe.old, luc.exe.pre-*)
Type: files; Name: "{app}\luc.exe.old"
Type: files; Name: "{app}\luc.exe.pre-*"
Type: files; Name: "{app}\*.tmp"
Type: files; Name: "{app}\luc_modules\*.tmp"
Type: files; Name: "{app}\luc_modules\*.part"
Type: filesandordirs; Name: "{app}\packages"
; libraries downloaded by "luc install -p" are not in the uninstall log
Type: files; Name: "{app}\luc_modules\discord.luc"
Type: files; Name: "{app}\luc_modules\ai.luc"
Type: files; Name: "{app}\luc_modules\bench_gpu_transformer.luc"
Type: files; Name: "{app}\luc_modules\blas.luc"
Type: files; Name: "{app}\luc_modules\data.luc"
Type: files; Name: "{app}\luc_modules\embedding.luc"
Type: files; Name: "{app}\luc_modules\forge.luc"
Type: files; Name: "{app}\luc_modules\gpu.luc"
Type: files; Name: "{app}\luc_modules\gpu_transformer.luc"
Type: files; Name: "{app}\luc_modules\lmio.luc"
Type: files; Name: "{app}\luc_modules\lmlog.luc"
Type: files; Name: "{app}\luc_modules\lmtrain.luc"
Type: files; Name: "{app}\luc_modules\lmtrain_gpu.luc"
Type: files; Name: "{app}\luc_modules\luaTL.luc"
Type: files; Name: "{app}\luc_modules\luatl_adapter.luc"
Type: files; Name: "{app}\luc_modules\luaTL_train.luc"
Type: files; Name: "{app}\luc_modules\matrix.luc"
Type: files; Name: "{app}\luc_modules\nn.luc"
Type: files; Name: "{app}\luc_modules\optim.luc"
Type: files; Name: "{app}\luc_modules\optim2.luc"
Type: files; Name: "{app}\luc_modules\positional.luc"
Type: files; Name: "{app}\luc_modules\rope.luc"
Type: files; Name: "{app}\luc_modules\tensor.luc"
Type: files; Name: "{app}\luc_modules\tensor2.luc"
Type: files; Name: "{app}\luc_modules\tokenizer.luc"
Type: files; Name: "{app}\luc_modules\train.luc"
Type: files; Name: "{app}\luc_modules\transformer.luc"
Type: files; Name: "{app}\luc_modules\README.txt"
; Sweep leftovers from older v1/v2 installs made without uninstalling first
Type: files; Name: "{app}\lucgcc.exe"
Type: files; Name: "{app}\luc-fallback.exe"
Type: files; Name: "{app}\rebuild.bat"
Type: files; Name: "{app}\build_log.txt"
Type: files; Name: "{app}\build_flags.txt"
Type: filesandordirs; Name: "{app}\sdl2"

[Messages]
; window title shows the version so users know what they are installing
SetupAppTitle=Setup - Luc {#SetupSetting("AppVersion")}
WelcomeLabel2=This will install LUC on your computer - a tiny scripting language packed into a single exe.%n%nSetup: everything is prebuilt - no compiler, no MSYS2, no downloads during setup. Click Install and LUC works immediately, just like installing Python.

[Code]
const
  // GMaintAction
  MA_NONE      = -1;   // fresh install or upgrade (different version)
  MA_MODIFY    = 0;    // add / remove components
  MA_REPAIR    = 1;    // restore everything, no pages
  MA_REINSTALL = 2;    // delete LUC's files, install again, no pages
  MA_UNINSTALL = 3;

var
  GMaintAction: Integer;
  GPrevDir, GPrevVer, GPrevComponents: String;
  GSelApplied: Boolean;
  GLibDone, GLibFailed, GReport: String;

// Looks up the previous install (uninstall registry key Luc_is1).
// Checks HKCU first, then HKLM (older installs may be admin-mode).
// Returns the display version, install dir and uninstaller path.
function PreviousInstall(var Ver, Dir, Uninst: String): Boolean;
var
  Key: String;
begin
  Key := 'Software\Microsoft\Windows\CurrentVersion\Uninstall\Luc_is1';
  if RegQueryStringValue(HKCU, Key, 'UninstallString', Uninst) then begin
    Result := True;
    if not RegQueryStringValue(HKCU, Key, 'DisplayVersion', Ver) then
      Ver := '';
    if not RegQueryStringValue(HKCU, Key, 'InstallLocation', Dir) then
      Dir := ExpandConstant('{userpf}\LUC');
  end else if RegQueryStringValue(HKLM, Key, 'UninstallString', Uninst) then begin
    Result := True;
    if not RegQueryStringValue(HKLM, Key, 'DisplayVersion', Ver) then
      Ver := '';
    if not RegQueryStringValue(HKLM, Key, 'InstallLocation', Dir) then
      Dir := ExpandConstant('{userpf}\LUC');
  end else
    Result := False;
end;

// Space-or-comma separated component list membership (case-insensitive).
function CompOn(const Sel, Name: String): Boolean;
var
  N: String;
begin
  N := LowerCase(Sel);
  StringChangeEx(N, ',', ' ', True);
  Result := Pos(' ' + LowerCase(Name) + ' ', ' ' + N + ' ') > 0;
end;

// Component names changed from a flat list (window, ailib, discord, vsext,
// lcode, lcodefont, source) to a tree. Accepts a snapshot written by any
// setup version and returns it with the current names; '' stays ''.
function NormComps(const S: String): String;
var
  LcodeOn: Boolean;
begin
  Result := '';
  if S = '' then
    Exit;
  Result := 'main';
  if CompOn(S, 'window') or CompOn(S, 'libs\window') then
    Result := Result + ',libs\window';
  if CompOn(S, 'ailib') or CompOn(S, 'libs\ai') then
    Result := Result + ',libs\ai';
  if CompOn(S, 'discord') or CompOn(S, 'libs\discord') then
    Result := Result + ',libs\discord';
  if CompOn(S, 'vsext') or CompOn(S, 'tools\vscode') then
    Result := Result + ',tools\vscode';
  LcodeOn := CompOn(S, 'lcode') or CompOn(S, 'tools\lcode');
  if LcodeOn then
    Result := Result + ',tools\lcode';
  if LcodeOn and (CompOn(S, 'lcodefont') or CompOn(S, 'tools\lcode\font')) then
    Result := Result + ',tools\lcode\font';
  if CompOn(S, 'source') then
    Result := Result + ',source';
end;

// One choice of the maintenance dialog: radio button + grey one-line hint.
function AddChoice(Form: TSetupForm; Top: Integer; const Title, Hint: String): TRadioButton;
var
  L: TLabel;
begin
  Result := TRadioButton.Create(Form);
  Result.Parent := Form;
  Result.Left := ScaleX(24);
  Result.Top := Top;
  Result.Width := Form.ClientWidth - ScaleX(48);
  Result.Height := ScaleY(18);
  Result.Caption := Title;

  L := TLabel.Create(Form);
  L.Parent := Form;
  L.Left := ScaleX(42);
  L.Top := Top + ScaleY(19);
  L.Caption := Hint;
  L.Font.Color := clGray;
end;

// Maintenance dialog when this same version is already installed.
// Returns MA_MODIFY, MA_REPAIR, MA_REINSTALL, MA_UNINSTALL or MA_NONE (cancel).
function AskMaintenance: Integer;
var
  Form: TSetupForm;
  HeadLbl, SubLbl: TLabel;
  RBMod, RBRep, RBRei, RBUni: TRadioButton;
  OkBtn, CancelBtn: TButton;
  VerLine: String;
begin
  Result := MA_NONE;
  // Inno 7: CreateCustomForm takes the size + keep-size flags
  Form := CreateCustomForm(ScaleX(500), ScaleY(330), True, True);
  try
    Form.Caption := 'Setup - Luc {#SetupSetting("AppVersion")}';
    Form.Position := poScreenCenter;

    if GPrevVer <> '' then
      VerLine := 'Version ' + GPrevVer
    else
      VerLine := 'Version {#SetupSetting("AppVersion")}';

    HeadLbl := TLabel.Create(Form);
    HeadLbl.Parent := Form;
    HeadLbl.Left := ScaleX(24);
    HeadLbl.Top := ScaleY(14);
    HeadLbl.Width := Form.ClientWidth - ScaleX(48);
    HeadLbl.Caption := 'LUC is already installed - what do you want to do?';
    HeadLbl.Font.Style := [fsBold];

    SubLbl := TLabel.Create(Form);
    SubLbl.Parent := Form;
    SubLbl.Left := ScaleX(24);
    SubLbl.Top := ScaleY(34);
    SubLbl.Width := Form.ClientWidth - ScaleX(48);
    SubLbl.Caption := VerLine + '  -  ' + GPrevDir;

    RBMod := AddChoice(Form, ScaleY(66), 'Modify',
      'Add or remove libraries and tools (new libraries come from GitHub).');
    RBRep := AddChoice(Form, ScaleY(112), 'Repair',
      'Restore damaged or missing files, fix PATH and file associations.');
    RBRei := AddChoice(Form, ScaleY(158), 'Reinstall',
      'Delete LUC''s files and install this version again, same settings.');
    RBUni := AddChoice(Form, ScaleY(204), 'Uninstall',
      'Remove LUC from this computer.');
    RBRep.Checked := True;

    OkBtn := TButton.Create(Form);
    OkBtn.Parent := Form;
    OkBtn.Left := Form.ClientWidth - ScaleX(186);
    OkBtn.Top := Form.ClientHeight - ScaleY(38);
    OkBtn.Width := ScaleX(85);
    OkBtn.Height := ScaleY(25);
    OkBtn.Caption := 'Continue';
    OkBtn.ModalResult := mrOk;
    OkBtn.Default := True;

    CancelBtn := TButton.Create(Form);
    CancelBtn.Parent := Form;
    CancelBtn.Left := Form.ClientWidth - ScaleX(93);
    CancelBtn.Top := Form.ClientHeight - ScaleY(38);
    CancelBtn.Width := ScaleX(73);
    CancelBtn.Height := ScaleY(25);
    CancelBtn.Caption := 'Cancel';
    CancelBtn.ModalResult := mrCancel;
    CancelBtn.Cancel := True;

    if Form.ShowModal = mrOk then begin
      if RBMod.Checked then Result := MA_MODIFY
      else if RBRep.Checked then Result := MA_REPAIR
      else if RBRei.Checked then Result := MA_REINSTALL
      else if RBUni.Checked then Result := MA_UNINSTALL;
    end;
  finally
    Form.Free;
  end;
end;

function InitializeSetup(): Boolean;
var
  Ver, Dir, Uninst, Saved: String;
  R: Integer;
begin
  Result := True;
  GMaintAction := MA_NONE;
  GPrevDir := ''; GPrevVer := ''; GPrevComponents := '';
  GSelApplied := False;
  GLibDone := ''; GLibFailed := ''; GReport := '';
  if WizardSilent() then
    Exit;   // scripted install: plain install, no dialog
  if not PreviousInstall(Ver, Dir, Uninst) then
    Exit;   // fresh install: normal wizard flow

  GPrevDir := Dir;
  GPrevVer := Ver;
  if not RegQueryStringValue(HKCU, 'Software\LUC', 'Components', Saved) then
    Saved := '';
  GPrevComponents := NormComps(Saved);

  // Different version installed: plain upgrade over the old folder.
  if (Ver <> '') and (Ver <> '{#SetupSetting("AppVersion")}') then
    Exit;

  GMaintAction := AskMaintenance();
  if GMaintAction = MA_UNINSTALL then begin
    // hand over to the existing uninstaller, then exit
    Exec(RemoveQuotes(Uninst), '', '', SW_SHOW, ewWaitUntilTerminated, R);
    Result := False;
  end else if GMaintAction = MA_NONE then
    Result := False;   // cancelled
end;

// [Registry] ValueData helper: which components are ticked right now
function ComponentSnapshot(Param: String): String;
begin
  Result := WizardSelectedComponents(False);
end;

// [Files] check helper: the ai / discord script libraries are laid from
// this setup only on a first install or an upgrade. Modify / Repair /
// Reinstall download them from GitHub instead (FetchLibraries).
function FromPayload: Boolean;
begin
  Result := GMaintAction = MA_NONE;
end;

function Pick(IsOn: Boolean; const Name: String): String;
begin
  if IsOn then
    Result := Name
  else
    Result := '!' + Name;
end;

// Ticks exactly the previous selection (also when the components page is
// skipped, as in Repair / Reinstall). Called from ShouldSkipPage, which the
// wizard always runs for the components page before showing or skipping it.
procedure ApplyPrevSelection;
var
  P: String;
begin
  if GSelApplied or (GPrevComponents = '') then
    Exit;
  GSelApplied := True;
  P := GPrevComponents;
  WizardSelectComponents(
    Pick(CompOn(P, 'libs\window'), 'libs\window') + ',' +
    Pick(CompOn(P, 'libs\ai'), 'libs\ai') + ',' +
    Pick(CompOn(P, 'libs\discord'), 'libs\discord') + ',' +
    Pick(CompOn(P, 'tools\vscode'), 'tools\vscode') + ',' +
    Pick(CompOn(P, 'tools\lcode'), 'tools\lcode') + ',' +
    Pick(CompOn(P, 'tools\lcode\font'), 'tools\lcode\font') + ',' +
    Pick(CompOn(P, 'source'), 'source'));
end;

function ShouldSkipPage(PageID: Integer): Boolean;
begin
  Result := False;
  if PageID = wpSelectComponents then
    ApplyPrevSelection;
  // existing install (upgrade or maintenance): same folder, license already accepted
  if (GPrevDir <> '') and
     ((PageID = wpWelcome) or (PageID = wpLicense) or (PageID = wpSelectDir)) then
    Result := True;
  // Repair / Reinstall: no pages at all - installation starts right after Continue
  if ((GMaintAction = MA_REPAIR) or (GMaintAction = MA_REINSTALL)) and
     ((PageID = wpSelectComponents) or (PageID = wpReady)) then
    Result := True;
  // Modify: the components page's button installs directly
  if (GMaintAction = MA_MODIFY) and (PageID = wpReady) then
    Result := True;
end;

// Maintenance and upgrades install into the previous folder.
procedure InitializeWizard();
begin
  if GPrevDir <> '' then WizardForm.DirEdit.Text := GPrevDir;
end;

procedure CurPageChanged(CurPageID: Integer);
begin
  if (CurPageID = wpSelectComponents) and (GMaintAction = MA_MODIFY) then begin
    WizardForm.SelectComponentsLabel.Caption :=
      'Tick to add, untick to remove. Libraries you add are downloaded from GitHub (window comes with this setup).';
    WizardForm.NextButton.Caption := SetupMessage(msgButtonInstall);
  end;
  if (CurPageID = wpFinished) and (GReport <> '') then begin
    WizardForm.FinishedLabel.Caption := GReport;
    WizardForm.AdjustLabelHeight(WizardForm.FinishedLabel);
    WizardForm.RunList.Top := WizardForm.FinishedLabel.Top + WizardForm.FinishedLabel.Height + ScaleY(12);
  end;
end;

// Deletes every name of a comma separated list from Dir (missing ones are fine).
procedure DeleteNames(const Dir, Names: String);
var
  Rest, Item: String;
  P: Integer;
begin
  Rest := Names;
  while Rest <> '' do begin
    P := Pos(',', Rest);
    if P > 0 then begin
      Item := Copy(Rest, 1, P - 1);
      Rest := Copy(Rest, P + 1, Length(Rest));
    end else begin
      Item := Rest;
      Rest := '';
    end;
    if Item <> '' then
      DeleteFile(AddBackslash(Dir) + Item);
  end;
end;

function Gone(const PrevSel, NewSel, Name: String): Boolean;
begin
  Result := CompOn(PrevSel, Name) and not CompOn(NewSel, Name);
end;

// Unticked in Modify (or on an upgrade): remove exactly that component's
// files. Runs before the new files are copied.
procedure RemoveDeselected(const PrevSel, NewSel: String);
var
  App: String;
begin
  App := ExpandConstant('{app}');
  if Gone(PrevSel, NewSel, 'libs\window') then
    DeleteNames(App, '{#WindowFiles}');     // luc.exe becomes the console build
  if Gone(PrevSel, NewSel, 'libs\ai') then
    DeleteNames(App + '\luc_modules', '{#AiFiles}');
  if Gone(PrevSel, NewSel, 'libs\discord') then
    DeleteFile(App + '\luc_modules\discord.luc');
  if Gone(PrevSel, NewSel, 'tools\vscode') then
    DelTree(ExpandConstant('{%USERPROFILE}\.vscode\extensions\hsusulist.luc'), True, True, True);
  if Gone(PrevSel, NewSel, 'tools\lcode') then begin
    DeleteFile(App + '\lcode.exe');
    DelTree(App + '\plugin\luc', True, True, True);
    DelTree(App + '\plugin\catppuccin', True, True, True);
  end;
  if Gone(PrevSel, NewSel, 'source') then
    DelTree(App + '\src', True, True, True);
end;

// Reinstall: delete everything this setup lays down, so the new copy
// starts clean. Kept: luc_modules libraries (refreshed from GitHub after
// the install, old copy kept if offline), user plugins and user modules,
// settings in %LOCALAPPDATA%\LUC.
procedure RemoveManagedFiles;
var
  App: String;
begin
  App := ExpandConstant('{app}');
  DeleteNames(App, 'luc.exe,lcode.exe,install.ps1,minisign.pub,{#WindowFiles}');
  DeleteFile(App + '\luc_modules\http.luc');
  DelTree(App + '\demos', True, True, True);
  DelTree(App + '\src', True, True, True);
  DelTree(App + '\plugin\luc', True, True, True);
  DelTree(App + '\plugin\catppuccin', True, True, True);
  DelTree(ExpandConstant('{%USERPROFILE}\.vscode\extensions\hsusulist.luc'), True, True, True);
end;

// Leftovers from interrupted updates and older setups.
procedure CleanLeftovers;
var
  App: String;
begin
  App := ExpandConstant('{app}');
  DeleteFile(App + '\luc.exe.old');
  DelTree(App + '\*.tmp', False, True, False);
  DelTree(App + '\luc_modules\*.tmp', False, True, False);
  DelTree(App + '\luc_modules\*.part', False, True, False);
  DelTree(App + '\packages', True, True, True);
end;

procedure DropFromSnapshot(const Comp: String);
var
  S: String;
begin
  if not RegQueryStringValue(HKCU, 'Software\LUC', 'Components', S) then
    Exit;
  S := ',' + S + ',';
  StringChangeEx(S, ',' + Comp + ',', ',', True);
  S := Copy(S, 2, Length(S) - 2);
  RegWriteStringValue(HKCU, 'Software\LUC', 'Components', S);
end;

procedure AddTo(var List: String; const Name: String);
begin
  if List <> '' then
    List := List + ', ';
  List := List + Name;
end;

// Runs "{app}\luc.exe install -p <Name>" (download from GitHub, hidden).
// luc only swaps files in after a complete download, so a failure leaves
// the copy that was already there untouched.
procedure FetchOne(const Comp, Name: String);
var
  RC: Integer;
begin
  if not WizardIsComponentSelected(Comp) then
    Exit;
  if (GMaintAction = MA_MODIFY) and CompOn(GPrevComponents, Comp) then
    Exit;   // Modify: already installed, untouched
  WizardForm.StatusLabel.Caption := 'Downloading the ' + Name + ' library from GitHub...';
  WizardForm.FilenameLabel.Caption := 'luc install -p ' + Name;
  RC := -1;
  if Exec(ExpandConstant('{app}\luc.exe'), 'install -p ' + Name, ExpandConstant('{app}'),
          SW_HIDE, ewWaitUntilTerminated, RC) and (RC = 0) then
    AddTo(GLibDone, Name)
  else begin
    AddTo(GLibFailed, Name);
    // Modify: not installed - keep it unticked so the next Modify offers it again
    if GMaintAction = MA_MODIFY then
      DropFromSnapshot(Comp);
  end;
end;

procedure FetchLibraries;
var
  Msg: String;
begin
  FetchOne('libs\ai', 'ai');
  FetchOne('libs\discord', 'discord');
  if GLibFailed <> '' then begin
    if GMaintAction = MA_MODIFY then
      Msg := 'These libraries were not installed.'
    else
      Msg := 'The copy already on this computer (if any) was kept.';
    SuppressibleMsgBox('Could not download from GitHub: ' + GLibFailed + #13#10#13#10 +
      'Check your internet connection. ' + Msg + #13#10 +
      'You can add them later with:  luc install -p <name>',
      mbError, MB_OK, IDOK);
  end;
end;

// Repair / Reinstall check: does the interpreter actually start?
function VerifyLuc: String;
var
  App: String;
  RC: Integer;
begin
  App := ExpandConstant('{app}');
  RC := -1;
  if not FileExists(App + '\luc.exe') then
    Result := 'luc.exe is MISSING'
  else if Exec(App + '\luc.exe', '--version', App, SW_HIDE, ewWaitUntilTerminated, RC) and (RC = 0) then
    Result := 'luc.exe starts correctly'
  else
    Result := 'luc.exe FAILED to start (exit code ' + IntToStr(RC) + ')';
  if WizardIsComponentSelected('libs\window') and not FileExists(App + '\SDL2.dll') then
    Result := Result + ', SDL2.dll is MISSING';
end;

procedure BuildReport;
begin
  if GMaintAction = MA_REPAIR then
    GReport := 'LUC has been repaired.'
  else
    GReport := 'LUC has been reinstalled.';
  GReport := GReport + #13#10#13#10 +
    '- every file of the installed components was restored' + #13#10 +
    '- PATH, LUC_PATH and file associations were registered again' + #13#10 +
    '- leftovers of interrupted updates were removed';
  if GLibDone <> '' then
    GReport := GReport + #13#10 + '- downloaded from GitHub: ' + GLibDone;
  if GLibFailed <> '' then
    GReport := GReport + #13#10 + '- could not download: ' + GLibFailed + ' (offline?)';
  GReport := GReport + #13#10 + '- check: ' + VerifyLuc;
end;

procedure WriteState;
var
  StateDir, AppPath, StateFile, StateJson: String;
begin
  // Mark this installation so "luc" starts the REPL instead of the
  // first-run installer, and so "luc install" knows LUC is present.
  StateDir := ExpandConstant('{localappdata}\LUC');
  ForceDirectories(StateDir);
  StateFile := StateDir + '\state.json';
  AppPath := ExpandConstant('{app}');
  StringChangeEx(AppPath, '\', '\\', True);
  StateJson := '{"version":"{#SetupSetting("AppVersion")}","installPath":"' + AppPath + '","source":"inno","installedAt":"' + GetDateTimeString('yyyy-mm-dd hh:nn:ss', #0, #0) + '"}';
  SaveStringToFile(StateFile, StateJson, False);
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssInstall then begin
    if GPrevDir <> '' then
      CleanLeftovers;
    if GMaintAction = MA_REINSTALL then
      RemoveManagedFiles
    else if GPrevComponents <> '' then
      RemoveDeselected(GPrevComponents, WizardSelectedComponents(False));
  end;
  if CurStep = ssPostInstall then begin
    WriteState;   // before running luc.exe below
    if (GMaintAction = MA_MODIFY) or (GMaintAction = MA_REPAIR) or (GMaintAction = MA_REINSTALL) then
      FetchLibraries;
    if (GMaintAction = MA_REPAIR) or (GMaintAction = MA_REINSTALL) then
      BuildReport;
  end;
end;

function ContainsPath(Path: string): Boolean;
var
  Current: string;
begin
  if not RegQueryStringValue(HKCU, 'Environment', 'Path', Current) then
    Current := '';
  Result := Pos(LowerCase(Path), LowerCase(Current)) > 0;
end;

// [Registry] check helper. NOTE: a check function referenced by a bare
// name in "Check: <name>" must be declared with NO parameters - Inno
// Setup derives the required prototype from the Check: expression
// itself (each argument written there adds one parameter).
function AddAppDirToPath: Boolean;
begin
  Result := not ContainsPath(ExpandConstant('{app}'));
end;

// [Files] check helper: the console build becomes luc.exe only when
// the "libs\window" component is NOT selected (otherwise the full SDL2
// build is installed as luc.exe instead).
function ConsoleSelected: Boolean;
begin
  Result := not WizardIsComponentSelected('libs\window');
end;

// [Files] check helper: skip copying the installer onto itself when
// it is being run from {app} (Windows cannot overwrite a running exe)
function NotSelfCopy: Boolean;
begin
  Result := CompareText(ExtractFilePath(ExpandConstant('{srcexe}')), AddBackslash(ExpandConstant('{app}'))) <> 0;
end;

// Called during uninstall: remove the install folder entry from PATH (if present)
procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  Current, AppDir, NewPath, Entry: string;
  P: Integer;
begin
  if CurUninstallStep <> usUninstall then
    Exit;
  if not RegQueryStringValue(HKCU, 'Environment', 'Path', Current) then
    Exit;
  AppDir := ExpandConstant('{app}');
  if Pos(LowerCase(AppDir), LowerCase(Current)) = 0 then
    Exit;
  NewPath := '';
  while Length(Current) > 0 do
  begin
    P := Pos(';', Current);
    if P > 0 then
    begin
      Entry := Copy(Current, 1, P - 1);
      Current := Copy(Current, P + 1, Length(Current));
    end
    else
    begin
      Entry := Current;
      Current := '';
    end;
    if (Length(Entry) > 0) and (LowerCase(Entry) <> LowerCase(AppDir)) then
    begin
      if Length(NewPath) > 0 then
        NewPath := NewPath + ';';
      NewPath := NewPath + Entry;
    end;
  end;
  RegWriteExpandStringValue(HKCU, 'Environment', 'Path', NewPath);
end;
