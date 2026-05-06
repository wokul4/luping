; ScreenRecorder 0.1.0-beta
; Inno Setup installer script

#define AppVersion GetFileVersion("dist\ScreenRecorder\VERSION.txt")

[Setup]
AppName=ScreenRecorder
AppVersion=0.1.0-beta
AppPublisher=ScreenRecorder Project
DefaultDirName={autopf}\ScreenRecorder
DefaultGroupName=ScreenRecorder
SetupIconFile=..\resources\icons\app_icon.ico
UninstallDisplayIcon={app}\ScreenRecorder.exe
Compression=lzma2/max
SolidCompression=yes
OutputDir=..\dist
OutputBaseFilename=ScreenRecorderSetup_0.1.0-beta
DisableProgramGroupPage=yes
DisableDirPage=no
PrivilegesRequired=admin

[Files]
Source: "..\dist\ScreenRecorder\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Dirs]
Name: "{app}\logs"
Name: "{app}\captures"
Name: "{app}\config"
Name: "{app}\assets"
Name: "{app}\docs"

[Icons]
Name: "{group}\ScreenRecorder"; Filename: "{app}\ScreenRecorder.exe"; WorkingDir: "{app}"
Name: "{group}\Uninstall ScreenRecorder"; Filename: "{uninstallexe}"
Name: "{commondesktop}\ScreenRecorder"; Filename: "{app}\ScreenRecorder.exe"; WorkingDir: "{app}"; Tasks: desktopicon

[Tasks]
Name: desktopicon; Description: "Create a &desktop shortcut"; GroupDescription: "Additional icons:"

[Run]
Filename: "{app}\ScreenRecorder.exe"; Description: "Launch ScreenRecorder"; Flags: postinstall nowait skipifsilent unchecked
