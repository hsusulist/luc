; Inno Setup Script for LUC
; Build with: iscc luc-installer.iss

[Setup]
AppName=Luc
AppVersion=0.1
DefaultDirName={userpf}\LUC
DefaultGroupName=LUC
OutputBaseFilename=luc-installer
Compression=lzma2
SolidCompression=yes
LicenseFile=LICENSE.txt
PrivilegesRequired=none
SetupIconFile=image.ico
UninstallDisplayIcon={app}\luc.exe
AppPublisher=Duy
AppPublisherURL=https://example.com
AppSupportURL=https://example.com
AppUpdatesURL=https://example.com

[Files]
Source: "dist\app\luc.exe"; DestDir: "{app}"
Source: "dist\app\*.dll"; DestDir: "{app}"

[Icons]
Name: "{group}\LUC"; Filename: "{app}\luc.exe"
Name: "{userdesktop}\LUC"; Filename: "{app}\luc.exe"

[Run]
Filename: "{app}\luc.exe"; Description: "Run LUC"; Flags: postinstall nowait
