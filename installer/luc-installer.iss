; LUC - Inno Setup script (PREBUILT, English, setup v3)
; update 2026-09-01: AppId, opt-in window/ai, maintenance dialog, start menu
; update 2026-09-02: ver 0.1, library management, offline packages
; update 2026-09-21: ver 0.2-beta1
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
; Maintenance: re-running this setup (or the "Luc Installer" Start Menu
; entry) compares versions first. A different installed version means a
; plain upgrade over the old folder (previous folder kept, previous
; components pre-ticked, no dialog, no uninstall-then-install).
; The same version shows a dialog with the installed folder + versions:
;   Install libraries - jumps straight to the components page, previous
;                       selection pre-ticked: tick to add a library,
;                       untick to remove it (files deleted, ticking again
;                       reinstalls fresh in the same run)
;   Fix               - same pages, pre-ticked: press Next through to
;                       reinstall this version over the previous folder
;   Uninstall         - remove LUC completely
; Default components: main only. "window", "ailib" and "discord" are opt-in
; (each has its own checkbox/index, unchecked by default); users
; can add them later without this setup via "luc install window|ai|discord".
; "net" needs no component: it is built into luc.exe itself (Winsock,
; no extra DLLs, no download) - "import net" works out of the box.
; Build the installer: run build_installer.ps1, or
;   ISCC installer\luc-installer.iss
;   (needs dist\app\luc-win.exe, dist\app\luc-core.exe,
;    dist\app\SDL2.dll - shipped prebuilt in this zip)
; Required files in installer\: LICENSE.txt, luc.ico,
;   lucdownload2.bmp, lucdownload.bmp

[Setup]
; AppId pins the uninstall registry key (Luc_is1). Same value the AppName
; fallback produced before, so installs made without this line stay detected.
AppId=Luc
AppName=Luc
AppVersion=0.2-beta1
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
ShowLanguageDialog=yes

[Languages]
Name: "en"; MessagesFile: "compiler:Default.isl"
#if FileExists(AddBackslash(CompilerPath) + "Languages\Vietnamese.isl")
Name: "vi"; MessagesFile: "compiler:Languages\Vietnamese.isl"
#endif

[Types]
Name: "full"; Description: "Download all"; Flags: iscustom

[Components]
; main (fixed) is the lightweight default: interpreter + core libraries
; (including built-in "net": TCP/HTTP/WebSocket, no download needed).
; Large libraries each get their OWN checkbox/index (unchecked = opt-in),
; kept in one block and separate from the tools below (vsext, source):
; tick only what you need instead of one bundled "download all".
; All three can be added later without this installer:
;   "luc install window|ai|discord".
Name: "main"; Description: "LUC interpreter + core libraries (string, list, math, bit32, JSON, buffer, IO, OS, task, coroutine, net) - prebuilt"; Types: full; Flags: fixed
Name: "window"; Description: "Window support - SDL2 2D graphics, PNG/JPG sprites, TTF text, WAV/OGG/MP3 sound + Pong demo (optional; add later with: luc install window)"
Name: "ailib"; Description: "lanternl AI library - 'import ai': neural nets, LMTrain, BPE tokenizer (optional; add later with: luc install ai)"
Name: "discord"; Description: "Discord bot library - 'import discord': bot, slash commands, gateway (optional; add later with: luc install discord)"
Name: "vsext"; Description: "VS Code extension - LUC syntax highlighting"; Types: full
Name: "lcode"; Description: "LCode editor - built-in code editor with file explorer (Start Menu shortcut)"; Types: full
Name: "source"; Description: "Keep the C source in the install folder (for developers)"; Types: full

[Files]
; Full build with SDL2 window support, installed as luc.exe
Source: "..\dist\app\luc-win.exe"; DestDir: "{app}"; DestName: "luc.exe"; Components: window; Flags: ignoreversion
; SDL2 runtime that goes with it (loaded from the install folder)
Source: "..\dist\app\SDL2.dll"; DestDir: "{app}"; Components: window; Flags: ignoreversion
; Satellite media DLLs (runtime-bound: TTF text, PNG/JPG images, WAV/OGG/MP3 sound)
Source: "..\dist\app\SDL2_ttf.dll"; DestDir: "{app}"; Components: window; Flags: ignoreversion skipifsourcedoesntexist
Source: "..\dist\app\SDL2_image.dll"; DestDir: "{app}"; Components: window; Flags: ignoreversion skipifsourcedoesntexist
Source: "..\dist\app\SDL2_mixer.dll"; DestDir: "{app}"; Components: window; Flags: ignoreversion skipifsourcedoesntexist
Source: "..\dist\app\libfreetype-6.dll"; DestDir: "{app}"; Components: window; Flags: ignoreversion skipifsourcedoesntexist
Source: "..\dist\app\libharfbuzz-0.dll"; DestDir: "{app}"; Components: window; Flags: ignoreversion skipifsourcedoesntexist
Source: "..\dist\app\libbz2-1.dll"; DestDir: "{app}"; Components: window; Flags: ignoreversion skipifsourcedoesntexist
Source: "..\dist\app\libpng16-16.dll"; DestDir: "{app}"; Components: window; Flags: ignoreversion skipifsourcedoesntexist
Source: "..\dist\app\zlib1.dll"; DestDir: "{app}"; Components: window; Flags: ignoreversion skipifsourcedoesntexist
Source: "..\dist\app\libbrotlidec.dll"; DestDir: "{app}"; Components: window; Flags: ignoreversion skipifsourcedoesntexist
Source: "..\dist\app\libbrotlicommon.dll"; DestDir: "{app}"; Components: window; Flags: ignoreversion skipifsourcedoesntexist
Source: "..\dist\app\libgraphite2.dll"; DestDir: "{app}"; Components: window; Flags: ignoreversion skipifsourcedoesntexist
Source: "..\dist\app\libglib-2.0-0.dll"; DestDir: "{app}"; Components: window; Flags: ignoreversion skipifsourcedoesntexist
Source: "..\dist\app\libintl-8.dll"; DestDir: "{app}"; Components: window; Flags: ignoreversion skipifsourcedoesntexist
Source: "..\dist\app\libpcre2-8-0.dll"; DestDir: "{app}"; Components: window; Flags: ignoreversion skipifsourcedoesntexist
Source: "..\dist\app\libiconv-2.dll"; DestDir: "{app}"; Components: window; Flags: ignoreversion skipifsourcedoesntexist
Source: "..\dist\app\libjpeg-8.dll"; DestDir: "{app}"; Components: window; Flags: ignoreversion skipifsourcedoesntexist
Source: "..\dist\app\libmpg123-0.dll"; DestDir: "{app}"; Components: window; Flags: ignoreversion skipifsourcedoesntexist
Source: "..\dist\app\libogg-0.dll"; DestDir: "{app}"; Components: window; Flags: ignoreversion skipifsourcedoesntexist
Source: "..\dist\app\libopus-0.dll"; DestDir: "{app}"; Components: window; Flags: ignoreversion skipifsourcedoesntexist
Source: "..\dist\app\libopusfile-0.dll"; DestDir: "{app}"; Components: window; Flags: ignoreversion skipifsourcedoesntexist
Source: "..\dist\app\libvorbis-0.dll"; DestDir: "{app}"; Components: window; Flags: ignoreversion skipifsourcedoesntexist
Source: "..\dist\app\libvorbisfile-3.dll"; DestDir: "{app}"; Components: window; Flags: ignoreversion skipifsourcedoesntexist
Source: "..\dist\app\libFLAC.dll"; DestDir: "{app}"; Components: window; Flags: ignoreversion skipifsourcedoesntexist
Source: "..\dist\app\libgcc_s_seh-1.dll"; DestDir: "{app}"; Components: window; Flags: ignoreversion skipifsourcedoesntexist
Source: "..\dist\app\libstdc++-6.dll"; DestDir: "{app}"; Components: window; Flags: ignoreversion skipifsourcedoesntexist
Source: "..\dist\app\libwinpthread-1.dll"; DestDir: "{app}"; Components: window; Flags: ignoreversion skipifsourcedoesntexist
; Default TTF font (Vietnamese-capable) used when the user picks no font
Source: "..\dist\app\DejaVuSans.ttf"; DestDir: "{app}"; Components: window; Flags: ignoreversion skipifsourcedoesntexist
; Console-only build, installed as luc.exe when "window" is unticked
Source: "..\dist\app\luc-core.exe"; DestDir: "{app}"; DestName: "luc.exe"; Check: ConsoleSelected; Flags: ignoreversion
; Runnable example scripts (window, net, discord, ai demos in subfolders)
Source: "..\demos\*"; DestDir: "{app}\demos"; Flags: ignoreversion recursesubdirs createallsubdirs
; This setup itself, so users can reopen the maintenance dialog
; (Install libraries / Fix / Uninstall) from the Start Menu without hunting for it.
; external: {srcexe} resolves at run time, not compile time
Source: "{srcexe}"; DestDir: "{app}"; DestName: "luc-installer.exe"; Flags: external ignoreversion; Check: NotSelfCopy
; lanternl AI library (loaded via 'import ai', also reachable through LUC_PATH)
; (discord.luc ships with the "discord" component, or separately via
; "luc install discord" - never bundled inside ailib)
Source: "..\luc_modules\*"; DestDir: "{app}\luc_modules"; Components: ailib; Flags: ignoreversion recursesubdirs createallsubdirs; Excludes: "discord.luc"
; Discord bot library (loaded via 'import discord'): own opt-in checkbox,
; own index - NOT auto-installed, NOT bundled with ailib.
Source: "..\luc_modules\discord.luc"; DestDir: "{app}\luc_modules"; Components: discord; Flags: ignoreversion
; Offline package source for "luc install ai|discord|window" (works with no internet)
Source: "..\packages\ai.lucpkg"; DestDir: "{app}\packages"; Flags: ignoreversion skipifsourcedoesntexist
Source: "..\packages\discord.lucpkg"; DestDir: "{app}\packages"; Flags: ignoreversion skipifsourcedoesntexist
Source: "..\dist\app\luc-win.exe"; DestDir: "{app}\packages\window"; DestName: "luc-win.exe"; Flags: ignoreversion
Source: "..\dist\app\SDL2.dll"; DestDir: "{app}\packages\window"; Flags: ignoreversion
Source: "..\dist\app\SDL2_ttf.dll"; DestDir: "{app}\packages\window"; Flags: ignoreversion skipifsourcedoesntexist
Source: "..\dist\app\SDL2_image.dll"; DestDir: "{app}\packages\window"; Flags: ignoreversion skipifsourcedoesntexist
Source: "..\dist\app\SDL2_mixer.dll"; DestDir: "{app}\packages\window"; Flags: ignoreversion skipifsourcedoesntexist
Source: "..\dist\app\libfreetype-6.dll"; DestDir: "{app}\packages\window"; Flags: ignoreversion skipifsourcedoesntexist
Source: "..\dist\app\libharfbuzz-0.dll"; DestDir: "{app}\packages\window"; Flags: ignoreversion skipifsourcedoesntexist
Source: "..\dist\app\libbz2-1.dll"; DestDir: "{app}\packages\window"; Flags: ignoreversion skipifsourcedoesntexist
Source: "..\dist\app\libpng16-16.dll"; DestDir: "{app}\packages\window"; Flags: ignoreversion skipifsourcedoesntexist
Source: "..\dist\app\zlib1.dll"; DestDir: "{app}\packages\window"; Flags: ignoreversion skipifsourcedoesntexist
Source: "..\dist\app\libbrotlidec.dll"; DestDir: "{app}\packages\window"; Flags: ignoreversion skipifsourcedoesntexist
Source: "..\dist\app\libbrotlicommon.dll"; DestDir: "{app}\packages\window"; Flags: ignoreversion skipifsourcedoesntexist
Source: "..\dist\app\libgraphite2.dll"; DestDir: "{app}\packages\window"; Flags: ignoreversion skipifsourcedoesntexist
Source: "..\dist\app\libglib-2.0-0.dll"; DestDir: "{app}\packages\window"; Flags: ignoreversion skipifsourcedoesntexist
Source: "..\dist\app\libintl-8.dll"; DestDir: "{app}\packages\window"; Flags: ignoreversion skipifsourcedoesntexist
Source: "..\dist\app\libpcre2-8-0.dll"; DestDir: "{app}\packages\window"; Flags: ignoreversion skipifsourcedoesntexist
Source: "..\dist\app\libiconv-2.dll"; DestDir: "{app}\packages\window"; Flags: ignoreversion skipifsourcedoesntexist
Source: "..\dist\app\libjpeg-8.dll"; DestDir: "{app}\packages\window"; Flags: ignoreversion skipifsourcedoesntexist
Source: "..\dist\app\libmpg123-0.dll"; DestDir: "{app}\packages\window"; Flags: ignoreversion skipifsourcedoesntexist
Source: "..\dist\app\libogg-0.dll"; DestDir: "{app}\packages\window"; Flags: ignoreversion skipifsourcedoesntexist
Source: "..\dist\app\libopus-0.dll"; DestDir: "{app}\packages\window"; Flags: ignoreversion skipifsourcedoesntexist
Source: "..\dist\app\libopusfile-0.dll"; DestDir: "{app}\packages\window"; Flags: ignoreversion skipifsourcedoesntexist
Source: "..\dist\app\libvorbis-0.dll"; DestDir: "{app}\packages\window"; Flags: ignoreversion skipifsourcedoesntexist
Source: "..\dist\app\libvorbisfile-3.dll"; DestDir: "{app}\packages\window"; Flags: ignoreversion skipifsourcedoesntexist
Source: "..\dist\app\libFLAC.dll"; DestDir: "{app}\packages\window"; Flags: ignoreversion skipifsourcedoesntexist
Source: "..\dist\app\libgcc_s_seh-1.dll"; DestDir: "{app}\packages\window"; Flags: ignoreversion skipifsourcedoesntexist
Source: "..\dist\app\libstdc++-6.dll"; DestDir: "{app}\packages\window"; Flags: ignoreversion skipifsourcedoesntexist
Source: "..\dist\app\libwinpthread-1.dll"; DestDir: "{app}\packages\window"; Flags: ignoreversion skipifsourcedoesntexist
Source: "..\dist\app\DejaVuSans.ttf"; DestDir: "{app}\packages\window"; Flags: ignoreversion skipifsourcedoesntexist
; VS Code extension (copied into the VS Code extensions folder)
Source: "..\vscode\*"; DestDir: "{%USERPROFILE}\.vscode\extensions\hsusulist.luc"; Components: vsext; Flags: ignoreversion recursesubdirs createallsubdirs
; C source for developers (explicit: src also contains stray exes that
; must NOT be shipped - they would silently bloat the installer)
Source: "..\src\*.h"; DestDir: "{app}\src"; Components: source; Flags: ignoreversion
Source: "..\src\*.c"; DestDir: "{app}\src"; Components: source; Flags: ignoreversion

[Icons]
Name: "{group}\LUC"; Filename: "{app}\luc.exe"
Name: "{group}\LCode"; Filename: "{app}\luc.exe"; Parameters: "--lcode ""{app}\demos"""; Components: lcode
Name: "{userdesktop}\LUC"; Filename: "{app}\luc.exe"
; Reopens the maintenance dialog (Install libraries / Fix / Uninstall) any time
Name: "{group}\Luc Installer"; Filename: "{app}\luc-installer.exe"

[Registry]
Root: HKCU; Subkey: "Environment"; ValueType: expandsz; ValueName: "Path"; ValueData: "{olddata};{app}"; Check: AddAppDirToPath
; LUC_PATH is set unconditionally: "luc install ai" must be able to drop
; modules into luc_modules even when the ailib component was not ticked
Root: HKCU; Subkey: "Environment"; ValueType: expandsz; ValueName: "LUC_PATH"; ValueData: "{app}\luc_modules"; Flags: uninsdeletevalue
; Snapshot of the ticked components: maintenance re-ticks them on
; "Install libraries" so re-running setup starts from the last selection
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
; "luc install window" swaps (luc.exe.old, luc.exe.pre-*)
Type: files; Name: "{app}\luc.exe.old"
Type: files; Name: "{app}\luc.exe.pre-*"
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
var
  GAppDir: String;
  // -1 none/fresh/upgrade, 0 libraries, 1 fix, 2 uninstall
  GMaintAction: Integer;
  GPrevDir, GPrevVer, GPrevComponents: String;

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

// Maintenance dialog when LUC is already installed.
// Returns 0 install libraries, 1 fix, 2 uninstall, -1 cancelled.
function AskMaintenance: Integer;
var
  Form: TSetupForm;
  HeadLbl, SubLbl: TLabel;
  RBLibs, RBFix, RBUninst: TRadioButton;
  OkBtn, CancelBtn: TButton;
  VerLine: String;
begin
  Result := -1;
  // Inno 7: CreateCustomForm takes the size + keep-size flags
  Form := CreateCustomForm(ScaleX(460), ScaleY(250), True, True);
  try
    Form.Caption := 'Setup - Luc {#SetupSetting("AppVersion")}';
    Form.Position := poScreenCenter;

    if GPrevVer <> '' then
      VerLine := 'Installed: ' + GPrevVer + '   -   This setup: {#SetupSetting("AppVersion")}'
    else
      VerLine := 'This setup: {#SetupSetting("AppVersion")}';

    HeadLbl := TLabel.Create(Form);
    HeadLbl.Parent := Form;
    HeadLbl.Left := ScaleX(24);
    HeadLbl.Top := ScaleY(14);
    HeadLbl.Width := Form.ClientWidth - ScaleX(48);
    HeadLbl.Caption := 'LUC is already installed in:';
    HeadLbl.Font.Style := [fsBold];

    SubLbl := TLabel.Create(Form);
    SubLbl.Parent := Form;
    SubLbl.Left := ScaleX(24);
    SubLbl.Top := ScaleY(34);
    SubLbl.Width := Form.ClientWidth - ScaleX(48);
    SubLbl.Caption := GPrevDir + #13#10 + VerLine;
    SubLbl.Autosize := False;
    SubLbl.Height := ScaleY(36);

    RBLibs := TRadioButton.Create(Form);
    RBLibs.Parent := Form;
    RBLibs.Left := ScaleX(24);
    RBLibs.Top := ScaleY(82);
    RBLibs.Width := Form.ClientWidth - ScaleX(48);
    RBLibs.Caption := 'Install libraries - jump straight to components to add or remove';
    RBLibs.Checked := True;

    RBFix := TRadioButton.Create(Form);
    RBFix.Parent := Form;
    RBFix.Left := ScaleX(24);
    RBFix.Top := ScaleY(112);
    RBFix.Width := Form.ClientWidth - ScaleX(48);
    RBFix.Caption := 'Fix - reinstall everything, pre-ticked (just press Next)';

    RBUninst := TRadioButton.Create(Form);
    RBUninst.Parent := Form;
    RBUninst.Left := ScaleX(24);
    RBUninst.Top := ScaleY(142);
    RBUninst.Width := Form.ClientWidth - ScaleX(48);
    RBUninst.Caption := 'Uninstall - remove LUC from this computer';

    OkBtn := TButton.Create(Form);
    OkBtn.Parent := Form;
    OkBtn.Left := Form.ClientWidth - ScaleX(166);
    OkBtn.Top := Form.ClientHeight - ScaleY(34);
    OkBtn.Width := ScaleX(73);
    OkBtn.Height := ScaleY(23);
    OkBtn.Caption := 'OK';
    OkBtn.ModalResult := mrOk;
    OkBtn.Default := True;

    CancelBtn := TButton.Create(Form);
    CancelBtn.Parent := Form;
    CancelBtn.Left := Form.ClientWidth - ScaleX(85);
    CancelBtn.Top := Form.ClientHeight - ScaleY(34);
    CancelBtn.Width := ScaleX(73);
    CancelBtn.Height := ScaleY(23);
    CancelBtn.Caption := 'Cancel';
    CancelBtn.ModalResult := mrCancel;
    CancelBtn.Cancel := True;

    if Form.ShowModal = mrOk then begin
      if RBLibs.Checked then Result := 0
      else if RBFix.Checked then Result := 1
      else if RBUninst.Checked then Result := 2;
    end;
  finally
    Form.Free;
  end;
end;

function InitializeSetup(): Boolean;
var
  Ver, Dir, Uninst: String;
  R: Integer;
begin
  Result := True;
  GMaintAction := -1;
  GPrevDir := ''; GPrevVer := ''; GPrevComponents := '';
  if WizardSilent() then
    Exit;   // Fix rerun or scripted install: plain install, no dialog
  if not PreviousInstall(Ver, Dir, Uninst) then
    Exit;   // fresh install: normal wizard flow

  GPrevDir := Dir;
  GPrevVer := Ver;
  if not RegQueryStringValue(HKCU, 'Software\LUC', 'Components', GPrevComponents) then
    GPrevComponents := '';

  // Different version installed: plain upgrade over the old folder.
  // No maintenance dialog, no uninstall-then-install dance.
  if (Ver <> '') and (Ver <> '{#SetupSetting("AppVersion")}') then
    Exit;

  GMaintAction := AskMaintenance();
  case GMaintAction of
    // 0 libraries and 1 fix share the wizard flow: components pre-ticked
    // with the previous selection, then a normal reinstall. No child
    // process relaunch (Exec proved unreliable here), so Fix cannot
    // "fail to start" - it is already running.
    0, 1: ;
    2: begin
         // uninstall: hand over to the existing uninstaller, then exit
         Exec(RemoveQuotes(Uninst), '', '', SW_SHOW,
              ewWaitUntilTerminated, R);
         Result := False;
       end;
  else
    Result := False;   // cancelled
  end;
end;

// [Registry] ValueData helper: which components are ticked right now
function ComponentSnapshot(Param: String): String;
begin
  Result := WizardSelectedComponents(False);
end;

// Libraries and Fix modes jump straight to the components page (skip welcome,
// license and folder - everything lands in the existing install).
function ShouldSkipPage(PageID: Integer): Boolean;
begin
  Result := False;
  if (GMaintAction = 0) or (GMaintAction = 1) then
    if (PageID = wpWelcome) or (PageID = wpLicense) or (PageID = wpSelectDir) then
      Result := True;
end;

// Maintenance and upgrades start in the previous folder (the user can
// still change it, except in libraries mode where the dir page is skipped).
procedure InitializeWizard();
begin
  if GPrevDir <> '' then WizardForm.DirEdit.Text := GPrevDir;
end;

// "Install libraries": start from the previous selection; unticking a
// library removes it (deleted in CurStepChanged before the new files land).
// Upgrades also start from the previous selection when we have a snapshot.
procedure CurPageChanged(CurPageID: Integer);
var
  Saved: String;
begin
  if (CurPageID = wpSelectComponents) and ((GMaintAction = 0) or (GPrevComponents <> '')) then
    if RegQueryStringValue(HKCU, 'Software\LUC', 'Components', Saved) and (Saved <> '') then
      WizardSelectComponents(Saved);
end;

// Space-or-comma separated component list membership.
function CompOn(const Sel, Name: String): Boolean;
var
  N: String;
begin
  N := Sel;
  StringChangeEx(N, ',', ' ', True);
  Result := Pos(' ' + Name + ' ', ' ' + N + ' ') > 0;
end;

// Deleting a deselected library: runs before the new files are copied,
// so ticking it again reinstalls it fresh in the same run.
procedure RemoveDeselected(const PrevSel, NewSel: String);
var
  App, ExtDir: String;
begin
  App := ExpandConstant('{app}');
  if CompOn(PrevSel, 'window') and not CompOn(NewSel, 'window') then begin
    DeleteFile(App + '\SDL2.dll');
    DeleteFile(App + '\SDL2_ttf.dll');
    DeleteFile(App + '\SDL2_image.dll');
    DeleteFile(App + '\SDL2_mixer.dll');
    DeleteFile(App + '\DejaVuSans.ttf');
  end;
  if (CompOn(PrevSel, 'ailib') and not CompOn(NewSel, 'ailib')) or
     (CompOn(PrevSel, 'discord') and not CompOn(NewSel, 'discord')) then
    DelTree(App + '\luc_modules', True, True, True);
  ExtDir := ExpandConstant('{%USERPROFILE}\.vscode\extensions\hsusulist.luc');
  if CompOn(PrevSel, 'vsext') and not CompOn(NewSel, 'vsext') then
    DelTree(ExtDir, True, True, True);
  if CompOn(PrevSel, 'source') and not CompOn(NewSel, 'source') then
    DelTree(App + '\src', True, True, True);
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if (CurStep = ssInstall) and (GPrevComponents <> '') then
    RemoveDeselected(GPrevComponents, WizardSelectedComponents(False));
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
// the "window" component is NOT selected (otherwise the full SDL2
// build is installed as luc.exe instead).
function ConsoleSelected: Boolean;
begin
  Result := not WizardIsComponentSelected('window');
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
