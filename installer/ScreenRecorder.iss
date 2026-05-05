# ScreenRecorder 0.1.0-beta
# Inno Setup installer script

#define AppVersion GetFileVersion("dist\ScreenRecorder\VERSION.txt")

[Setup]
AppName=ScreenRecorder
AppVersion=0.1.0-beta
AppPublisher=ScreenRecorder Project
DefaultDirName={autopf}\ScreenRecorder
DefaultGroupName=ScreenRecorder
UninstallDisplayIcon={app}\ScreenRecorder.exe
Compression=lzma2/max
SolidCompression=yes
OutputDir=..\dist
OutputBaseFilename=ScreenRecorderSetup_0.1.0-beta
DisableProgramGroupPage=yes
PrivilegesRequired=admin

[Files]
Source: "..\dist\ScreenRecorder\ScreenRecorder.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\dist\ScreenRecorder\*.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\dist\ScreenRecorder\config\settings.json"; DestDir: "{app}\config"; Flags: ignoreversion
Source: "..\dist\ScreenRecorder\README.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\dist\ScreenRecorder\VERSION.txt"; DestDir: "{app}"; Flags: ignoreversion

[Dirs]
Name: "{app}\logs"
Name: "{app}\captures"
Name: "{app}\config"

[Icons]
Name: "{group}\ScreenRecorder"; Filename: "{app}\ScreenRecorder.exe"
Name: "{group}\Uninstall ScreenRecorder"; Filename: "{uninstallexe}"
Name: "{commondesktop}\ScreenRecorder"; Filename: "{app}\ScreenRecorder.exe"; Tasks: desktopicon

[Tasks]
Name: desktopicon; Description: "Create a &desktop shortcut"; GroupDescription: "Additional icons:"

[Run]
Filename: "{app}\ScreenRecorder.exe"; Description: "Launch ScreenRecorder"; Flags: postinstall nowait skipifsilent unchecked
