; ============================================================
; LUC - Inno Setup script (simplified, English)
; Pages: Language -> Welcome -> License -> Components ->
;        Install folder -> Ready -> Installing -> Finish
;
; Build: run build_installer.ps1 (or ISCC installer\luc-installer.iss)
;
; Required files in installer\: LICENSE.txt, luc.ico,
;   lucdownload2.bmp, lucdownload.bmp
; ============================================================

[Setup]
AppName=Luc
AppVersion=0.1
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
ShowLanguageDialog=yes

[Languages]
Name: "en"; MessagesFile: "compiler:Default.isl"
#if FileExists(AddBackslash(CompilerPath) + "Languages\Vietnamese.isl")
Name: "vi"; MessagesFile: "compiler:Languages\Vietnamese.isl"
#endif

[Types]
Name: "full"; Description: "Download all"; Flags: iscustom

[Components]
Name: "main"; Description: "LUC interpreter + all libraries (string, list, math, bit32, JSON, buffer, IO, OS, task, coroutine)"; Types: full; Flags: fixed
Name: "window"; Description: "Window support - 2D graphics (installs SDL2.dll)"; Types: full
Name: "vsext"; Description: "VS Code extension - LUC syntax highlighting"; Types: full
Name: "source"; Description: "C source code (luc.h, luc_core.c, luc_libs.c)"; Types: full

[Files]
; Full build (with window lib) + SDL2.dll - only when window support is kept
Source: "..\dist\app\luc.exe"; DestDir: "{app}"; Components: window
Source: "..\dist\app\SDL2.dll"; DestDir: "{app}"; Components: window
; Core build installed as luc.exe when window support is unticked
Source: "..\dist\app\luc-core.exe"; DestDir: "{app}"; DestName: "luc.exe"; Check: not IsComponentSelected('window')
; C sources
Source: "..\src\luc.h"; DestDir: "{app}\src"; Components: source
Source: "..\src\luc_core.c"; DestDir: "{app}\src"; Components: source
Source: "..\src\luc_libs.c"; DestDir: "{app}\src"; Components: source
; VS Code extension (copied into the VS Code extensions folder)
Source: "..\vscode\*"; DestDir: "{%USERPROFILE}\.vscode\extensions\hsusulist.luc-language"; Components: vsext; Flags: recursesubdirs createallsubdirs

[Icons]
Name: "{group}\LUC"; Filename: "{app}\luc.exe"
Name: "{userdesktop}\LUC"; Filename: "{app}\luc.exe"

[Registry]
Root: HKCU; Subkey: "Environment"; ValueType: expandsz; ValueName: "Path"; ValueData: "{olddata};{app}"; Check: not ContainsPath(ExpandConstant('{app}'))

[Run]
Filename: "{app}"; Description: "Open the LUC installation folder"; Flags: postinstall skipifsilent unchecked

[UninstallDelete]
Type: filesandordirs; Name: "{%USERPROFILE}\.vscode\extensions\hsusulist.luc-language"

[Messages]
WelcomeLabel2=This will install LUC on your computer - a tiny scripting language packed into a single exe.%n%nIt is recommended that you close all other applications before continuing.

[Code]
function ContainsPath(Path: string): Boolean;
var
  Current: string;
begin
  if not RegQueryStringValue(HKCU, 'Environment', 'Path', Current) then
    Current := '';
  Result := Pos(LowerCase(Path), LowerCase(Current)) > 0;
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
