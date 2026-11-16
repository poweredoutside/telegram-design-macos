#define AppName "Telegram macOS Design"
#define AppVersion "7.4.4"
#ifndef PayloadDir
  #define PayloadDir SourcePath + "..\..\out\Debug"
#endif

[Setup]
AppId={{AE8B9692-D9D9-43B0-B1C9-8923E9C75E2B}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion} (Debug)
AppPublisher=poweredoutside
DefaultDirName={localappdata}\Programs\{#AppName}
DefaultGroupName={#AppName}
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.17763
OutputDir=..\..\out\setup
OutputBaseFilename=Telegram-macOS-Design-Setup-x64
SetupIconFile=..\Resources\art\icon256.ico
UninstallDisplayIcon={app}\Telegram.exe
UninstallDisplayName={#AppName}
VersionInfoVersion={#AppVersion}.0
VersionInfoDescription={#AppName} installer (Debug)
Compression=lzma2/normal
SolidCompression=yes
WizardStyle=modern dynamic
DisableWelcomePage=no
DisableDirPage=no
DisableProgramGroupPage=no
AllowNoIcons=yes
CloseApplications=no
RestartApplications=no
LicenseFile=..\..\LICENSE
InfoBeforeFile=setup_macos_design_info.txt

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "russian"; MessagesFile: "compiler:Languages\Russian.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"

[Dirs]
Name: "{app}\TelegramForcePortable"; Flags: uninsneveruninstall

[Files]
Source: "{#PayloadDir}\Telegram.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#PayloadDir}\modules\x64\d3d\d3dcompiler_47.dll"; DestDir: "{app}\modules\x64\d3d"; Flags: ignoreversion
Source: "..\..\LICENSE"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\..\LEGAL"; DestDir: "{app}"; Flags: ignoreversion
Source: "setup_macos_design_info.txt"; DestDir: "{app}"; DestName: "Readme.txt"; Flags: ignoreversion

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\Telegram.exe"; Parameters: "-noupdate"; WorkingDir: "{app}"
Name: "{group}\{cm:UninstallProgram,{#AppName}}"; Filename: "{uninstallexe}"
Name: "{userdesktop}\{#AppName}"; Filename: "{app}\Telegram.exe"; Parameters: "-noupdate"; WorkingDir: "{app}"; Tasks: desktopicon

[Run]
Filename: "{app}\Telegram.exe"; Parameters: "-noupdate"; WorkingDir: "{app}"; Description: "{cm:LaunchProgram,{#AppName}}"; Flags: nowait postinstall skipifsilent
