; Walls Cave Survey Software - Inno Setup Script
; For CI/CD builds

#define MyAppName "Walls"
#define MyAppVersion "2.2.1"
#define MyAppPublisher "Walls Cave Survey"
#define MyAppURL "https://github.com/wallscavesurvey/walls"
#define MyAppExeName "Walls32.exe"

[Setup]
AppId={{B8B0A649-478B-4F72-B710-B10AA12ACCA6}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}/issues
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
AllowNoIcons=yes
OutputDir=..\bin
OutputBaseFilename=WallsInstaller
Compression=lzma
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x86compatible
ArchitecturesInstallIn64BitMode=

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
; Main executables
Source: "..\bin\Walls32.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\bin\walls3d.exe"; DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist
Source: "..\bin\walls2D.exe"; DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist

; DLLs
Source: "..\bin\wallsef.dll"; DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist
Source: "..\bin\wallexp.dll"; DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist
Source: "..\bin\wallgps.dll"; DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist
Source: "..\bin\wallsvg.dll"; DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist
Source: "..\bin\wallshp.dll"; DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist
Source: "..\bin\wallcss.dll"; DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist

; Documentation
Source: "..\bin\walls32.chm"; DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist
Source: "..\bin\Walls_manual.pdf"; DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist

; Additional files from WallsInstaller folder
Source: "SVGView.exe"; DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist
Source: "Postinstall.rtf"; DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{group}\Walls 3D Viewer"; Filename: "{app}\walls3d.exe"
Name: "{group}\Walls 2D"; Filename: "{app}\walls2D.exe"
Name: "{group}\{cm:UninstallProgram,{#MyAppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Registry]
Root: HKCR; Subkey: ".wpj"; ValueType: string; ValueName: ""; ValueData: "WallsProject"; Flags: uninsdeletevalue
Root: HKCR; Subkey: "WallsProject"; ValueType: string; ValueName: ""; ValueData: "Walls Project File"; Flags: uninsdeletekey
Root: HKCR; Subkey: "WallsProject\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\{#MyAppExeName},2"
Root: HKCR; Subkey: "WallsProject\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#MyAppExeName}"" ""%1"""

Root: HKCR; Subkey: ".srv"; ValueType: string; ValueName: ""; ValueData: "WallsSurvey"; Flags: uninsdeletevalue
Root: HKCR; Subkey: "WallsSurvey"; ValueType: string; ValueName: ""; ValueData: "Walls Survey File"; Flags: uninsdeletekey
Root: HKCR; Subkey: "WallsSurvey\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\{#MyAppExeName},2"
Root: HKCR; Subkey: "WallsSurvey\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#MyAppExeName}"" ""%1"""

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#StringChange(MyAppName, '&', '&&')}}"; Flags: nowait postinstall skipifsilent
