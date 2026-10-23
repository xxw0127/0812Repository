; Inno Setup 6 template. Install Inno Setup, then run:
;   "C:\Program Files (x86)\Inno Setup 6\ISCC.exe" installer\chatbox.iss
; This machine does not currently have ISCC installed, so package_client.ps1
; produces a portable ZIP instead.

#define MyAppName "ChatBox"
#define MyAppVersion "0.2.1"
#define MyAppExeName "chatbox.exe"

[Setup]
AppId={{8C4F7B6E-6E44-4B7E-9C3A-5A2F6B7C8D90}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
DefaultDirName={autopf}\ChatBox
DefaultGroupName={#MyAppName}
OutputBaseFilename=chatbox-{#MyAppVersion}-setup
Compression=lzma
SolidCompression=yes
ArchitecturesInstallIn64BitMode=x64compatible
SetupIconFile=..\resources\app.ico
UninstallDisplayIcon={app}\{#MyAppExeName}

[Files]
Source: "..\build\release\*"; DestDir: "{app}"; Flags: recursesubdirs createallsubdirs

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Tasks]
Name: "desktopicon"; Description: "创建桌面快捷方式"; GroupDescription: "附加图标："

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "启动 {#MyAppName}"; Flags: nowait postinstall skipifsilent
