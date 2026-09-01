; ============================================================
; LUC - Inno Setup script (PREBUILT, English, setup v3)
;
; Setup v3 changes EVERYTHING about how window support works:
; the installer now ships READY-TO-RUN binaries built ahead of
; time on the developer's machine (build_installer.ps1):
;
;   luc.exe     - full build with SDL2 window support (when the
;                 "window" component is ticked) + SDL2.dll
;   luc-core.exe - console-only build (installed as luc.exe
;                 when "window" is unticked)
;
; The user's PC needs NOTHING: no compiler, no MSYS2, no MinGW,
; no downloads during setup. Ticking "Install" and clicking Next
; is enough - exactly like installing Python. The old setup-time
; compile (v1/v2: probing gcc, lucgcc.exe silent-fail helper,
; rebuild.bat, install-time SDL2 headers) is GONE.
;
; The old Finish-page bug is fixed as well: "Open the LUC
; installation folder" used to call CreateProcess() on the
; FOLDER itself (-> "CreateProcess failed; code 5. Access is
; denied."). It now uses the shellexec flag so Explorer opens
; the folder instead.
;
; Pages: Language -> Welcome -> License -> Components ->
;        Install folder -> Ready -> Installing -> Finish
;
; Build the installer: run build_installer.ps1, or
;   ISCC installer\luc-installer.iss
;   (needs dist\app\luc-win.exe, dist\app\luc-core.exe,
;    dist\app\SDL2.dll - shipped prebuilt in this zip)
;
; Required files in installer\: LICENSE.txt, luc.ico,
;   lucdownload2.bmp, lucdownload.bmp
; ============================================================

[Setup]
AppName=Luc
AppVersion=0.1.2
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
WizardImageFile=lucdownload2.bmp
WizardSmallImageFile=lucdownload.bmp
WizardImageStretch=yes
WizardStyle=modern
UninstallDisplayIcon={app}\luc.exe
ChangesEnvironment=yes
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
Name: "main"; Description: "LUC interpreter (prebuilt - installs and runs immediately, nothing else to download)"; Types: full; Flags: fixed
Name: "window"; Description: "Window support - SDL2 2D graphics, Pong demo (prebuilt, no compiler needed)"; Types: full
Name: "ailib"; Description: "lanternl AI library - 'import ai': neural nets, LMTrain, BPE tokenizer"; Types: full
Name: "vsext"; Description: "VS Code extension - LUC syntax highlighting"; Types: full
Name: "source"; Description: "Keep the C source in the install folder (for developers)"; Types: full

[Files]
; Full build with SDL2 window support, installed as luc.exe
Source: "..\dist\app\luc-win.exe"; DestDir: "{app}"; DestName: "luc.exe"; Components: window; Flags: ignoreversion
; SDL2 runtime that goes with it (loaded from the install folder)
Source: "..\dist\app\SDL2.dll"; DestDir: "{app}"; Components: window; Flags: ignoreversion
; Console-only build, installed as luc.exe when "window" is unticked
Source: "..\dist\app\luc-core.exe"; DestDir: "{app}"; DestName: "luc.exe"; Check: ConsoleSelected; Flags: ignoreversion
; Runnable example scripts (hello, json, pong)
Source: "..\demos\*"; DestDir: "{app}\demos"; Flags: ignoreversion
; lanternl AI library (loaded via 'import ai', also reachable through LUC_PATH)
Source: "..\luc_modules\*"; DestDir: "{app}\luc_modules"; Components: ailib; Flags: ignoreversion recursesubdirs createallsubdirs
; VS Code extension (copied into the VS Code extensions folder)
Source: "..\vscode\*"; DestDir: "{%USERPROFILE}\.vscode\extensions\hsusulist.luc-language"; Components: vsext; Flags: ignoreversion recursesubdirs createallsubdirs
; C source for developers
Source: "..\src\*"; DestDir: "{app}\src"; Components: source; Flags: ignoreversion

[Icons]
Name: "{group}\LUC"; Filename: "{app}\luc.exe"
Name: "{userdesktop}\LUC"; Filename: "{app}\luc.exe"

[Registry]
Root: HKCU; Subkey: "Environment"; ValueType: expandsz; ValueName: "Path"; ValueData: "{olddata};{app}"; Check: AddAppDirToPath
Root: HKCU; Subkey: "Environment"; ValueType: expandsz; ValueName: "LUC_PATH"; ValueData: "{app}\luc_modules"; Components: ailib; Flags: uninsdeletevalue

[Run]
; shellexec is REQUIRED here: CreateProcess() cannot open a folder
; (that was the "CreateProcess failed; code 5. Access is denied."
; error on the v1/v2 Finish page). With shellexec, Explorer opens it.
Filename: "{app}"; Description: "Open the LUC installation folder"; Flags: postinstall shellexec skipifsilent unchecked

[UninstallDelete]
; VS Code extension was installed outside {app}
Type: filesandordirs; Name: "{%USERPROFILE}\.vscode\extensions\hsusulist.luc-language"
; Sweep leftovers from older v1/v2 installs made without uninstalling first
Type: files; Name: "{app}\lucgcc.exe"
Type: files; Name: "{app}\luc-fallback.exe"
Type: files; Name: "{app}\rebuild.bat"
Type: files; Name: "{app}\build_log.txt"
Type: files; Name: "{app}\build_flags.txt"
Type: filesandordirs; Name: "{app}\sdl2"

[Messages]
WelcomeLabel2=This will install LUC on your computer - a tiny scripting language packed into a single exe.%n%nSetup v3: everything is prebuilt - no compiler, no MSYS2, no downloads during setup. Click Install and LUC works immediately, just like installing Python.

[Code]
var
  GAppDir: String;

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
