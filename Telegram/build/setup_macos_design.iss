#define AppName "Telega Desktop"
#define LegacyAppName "Telegram macOS Design"
#define AppGuid "{AE8B9692-D9D9-43B0-B1C9-8923E9C75E2B}"
#define AppModelId "PoweredOutside.TelegaDesktop"
#define ProtocolAppId "TelegaDesktop"
#ifndef PayloadDir
  #define PayloadDir SourcePath + "..\..\out\Release"
#endif
#define FileVersion GetFileVersion(PayloadDir + "\Telegram.exe")
#define AppVersion Copy(FileVersion, 1, RPos(".", FileVersion) - 1)
#ifdef DebugPayload
  #define BuildLabel " (Debug)"
  #define SetupFilename "Telega-Desktop-Setup-x64-Debug"
#else
  #ifdef TestCredentials
    #define BuildLabel " (Release test)"
  #else
    #define BuildLabel " RELEASE"
  #endif
  #define SetupFilename "Telega-Desktop-Setup-x64"
#endif

[Setup]
AppId={{#AppGuid}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion} x64{#BuildLabel}
AppPublisher=poweredoutside
AppPublisherURL=https://github.com/poweredoutside/telegram-design-macos
AppUpdatesURL=https://github.com/poweredoutside/telegram-design-macos/releases
DefaultDirName={localappdata}\Programs\{#AppName}
DefaultGroupName={#AppName}
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=6.1
#ifdef DebugPayload
OutputDir=..\..\out\setup
#else
OutputDir=..\..\out\setup-release
#endif
OutputBaseFilename={#SetupFilename}
SetupIconFile=..\Resources\art\telega256.ico
UninstallDisplayIcon={app}\Telegram.exe
UninstallDisplayName={#AppName}
VersionInfoVersion={#FileVersion}
VersionInfoProductTextVersion={#AppVersion} x64{#BuildLabel}
VersionInfoDescription={#AppName} installer{#BuildLabel}
Compression=lzma2/normal
SolidCompression=yes
WizardStyle=modern dynamic
ShowLanguageDialog=yes
UsePreviousLanguage=no
DisableWelcomePage=no
DisableDirPage=no
DisableProgramGroupPage=no
UsePreviousGroup=no
AllowNoIcons=yes
CloseApplications=no
RestartApplications=no
ChangesAssociations=yes
SetupLogging=yes
LicenseFile=..\..\LICENSE

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "russian"; MessagesFile: "compiler:Languages\Russian.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"
Name: "tgwsproxy"; Description: "{cm:AutoTgWsProxy}"; Flags: unchecked

[CustomMessages]
english.AutoTgWsProxy=Use Tg Ws Proxy automatically (starts with {#AppName})
russian.AutoTgWsProxy=Использовать Tg Ws Proxy автоматически (запускать вместе с {#AppName})
english.FileNotReplaceable=Setup cannot replace %1. Exit this copy of {#AppName} from the system tray, check that the file is writable, and run Setup again.
russian.FileNotReplaceable=Не удалось заменить %1. Закройте эту копию {#AppName} через значок в области уведомлений, проверьте доступ на запись и запустите установщик снова.
english.WindowsVersionRequired={#AppName} requires Windows 10 build 17763 or later.
russian.WindowsVersionRequired=Для {#AppName} нужна Windows 10 сборки 17763 или новее.
english.WindowsVersionUnknown=Setup could not determine the Windows version.
russian.WindowsVersionUnknown=Установщик не смог определить версию Windows.

[Dirs]
Name: "{app}\TelegramForcePortable"; Flags: uninsneveruninstall
Name: "{app}\tgwsproxy.auto"; Tasks: tgwsproxy

[InstallDelete]
Type: dirifempty; Name: "{app}\tgwsproxy.auto"; Tasks: not tgwsproxy
Type: files; Name: "{userdesktop}\{#LegacyAppName}.lnk"; Check: UpgradingLegacyInstall
Type: files; Name: "{userprograms}\{#LegacyAppName}\{#LegacyAppName}.lnk"; Check: UpgradingLegacyInstall
Type: files; Name: "{userprograms}\{#LegacyAppName}\Uninstall {#LegacyAppName}.lnk"; Check: UpgradingLegacyInstall
Type: files; Name: "{userprograms}\{#LegacyAppName}\Деинсталлировать {#LegacyAppName}.lnk"; Check: UpgradingLegacyInstall
Type: dirifempty; Name: "{userprograms}\{#LegacyAppName}"; Check: UpgradingLegacyInstall

[Files]
Source: "{#PayloadDir}\Telegram.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#PayloadDir}\Updater.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#PayloadDir}\modules\x64\d3d\d3dcompiler_47.dll"; DestDir: "{app}\modules\x64\d3d"; Flags: ignoreversion
Source: "..\Resources\winrc\tgwsproxy-license.txt"; DestDir: "{app}"; DestName: "TgWsProxy-LICENSE.txt"; Flags: ignoreversion
Source: "..\..\LICENSE"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\..\LEGAL"; DestDir: "{app}"; Flags: ignoreversion
Source: "setup_macos_design_info.txt"; DestDir: "{app}"; DestName: "Readme.txt"; Flags: ignoreversion

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\Telegram.exe"; WorkingDir: "{app}"; AppUserModelID: "{#AppModelId}"
Name: "{group}\{cm:UninstallProgram,{#AppName}}"; Filename: "{uninstallexe}"
Name: "{userdesktop}\{#AppName}"; Filename: "{app}\Telegram.exe"; WorkingDir: "{app}"; AppUserModelID: "{#AppModelId}"; Tasks: desktopicon

[Registry]
Root: HKCU; Subkey: "Software\{#ProtocolAppId}"; Flags: uninsdeletekeyifempty
Root: HKCU; Subkey: "Software\{#ProtocolAppId}\Capabilities"; ValueType: string; ValueName: "ApplicationName"; ValueData: "{#AppName}"; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\{#ProtocolAppId}\Capabilities"; ValueType: string; ValueName: "ApplicationDescription"; ValueData: "{#AppName}"
Root: HKCU; Subkey: "Software\{#ProtocolAppId}\Capabilities\UrlAssociations"; ValueType: string; ValueName: "tg"; ValueData: "{#ProtocolAppId}.tg"
Root: HKCU; Subkey: "Software\{#ProtocolAppId}\Capabilities\UrlAssociations"; ValueType: string; ValueName: "ton"; ValueData: "{#ProtocolAppId}.ton"
Root: HKCU; Subkey: "Software\{#ProtocolAppId}\Capabilities\UrlAssociations"; ValueType: string; ValueName: "tonsite"; ValueData: "{#ProtocolAppId}.tonsite"
Root: HKCU; Subkey: "Software\RegisteredApplications"; ValueType: string; ValueName: "{#AppName}"; ValueData: "SOFTWARE\{#ProtocolAppId}\Capabilities"; Flags: uninsdeletevalue

Root: HKCU; Subkey: "Software\Classes\{#ProtocolAppId}.tg"; ValueType: string; ValueData: "{#AppName}"; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\{#ProtocolAppId}.tg"; ValueType: string; ValueName: "URL Protocol"; ValueData: ""
Root: HKCU; Subkey: "Software\Classes\{#ProtocolAppId}.tg\DefaultIcon"; ValueType: string; ValueData: """{app}\Telegram.exe"",0"
Root: HKCU; Subkey: "Software\Classes\{#ProtocolAppId}.tg\shell\open\command"; ValueType: string; ValueData: """{app}\Telegram.exe"" -- ""%1"""
Root: HKCU; Subkey: "Software\Classes\{#ProtocolAppId}.ton"; ValueType: string; ValueData: "{#AppName}"; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\{#ProtocolAppId}.ton"; ValueType: string; ValueName: "URL Protocol"; ValueData: ""
Root: HKCU; Subkey: "Software\Classes\{#ProtocolAppId}.ton\DefaultIcon"; ValueType: string; ValueData: """{app}\Telegram.exe"",0"
Root: HKCU; Subkey: "Software\Classes\{#ProtocolAppId}.ton\shell\open\command"; ValueType: string; ValueData: """{app}\Telegram.exe"" -- ""%1"""
Root: HKCU; Subkey: "Software\Classes\{#ProtocolAppId}.tonsite"; ValueType: string; ValueData: "{#AppName}"; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\{#ProtocolAppId}.tonsite"; ValueType: string; ValueName: "URL Protocol"; ValueData: ""
Root: HKCU; Subkey: "Software\Classes\{#ProtocolAppId}.tonsite\DefaultIcon"; ValueType: string; ValueData: """{app}\Telegram.exe"",0"
Root: HKCU; Subkey: "Software\Classes\{#ProtocolAppId}.tonsite\shell\open\command"; ValueType: string; ValueData: """{app}\Telegram.exe"" -- ""%1"""

[Run]
Filename: "{app}\Telegram.exe"; WorkingDir: "{app}"; Description: "{cm:LaunchProgram,{#AppName}}"; Flags: nowait postinstall skipifsilent

[Code]
const
  MinimumWindowsMajorMinor = $000A0000;
  MinimumWindowsBuild = 17763;
  GenericWrite = $40000000;
  ShareReadWriteDelete = 7;
  OpenExisting = 3;
  FileAttributeNormal = $80;
  InvalidHandleValue = -1;
  UninstallKey = 'Software\Microsoft\Windows\CurrentVersion\Uninstall\{#AppGuid}_is1';

function InitializeSetup: Boolean;
var
  KernelPath: String;
  VersionMS, VersionLS, Build: Cardinal;
begin
  KernelPath := ExpandConstant('{sysnative}\ntoskrnl.exe');
  if not GetVersionNumbers(KernelPath, VersionMS, VersionLS) then
  begin
    Log('Could not read Windows kernel file version: ' + KernelPath);
    SuppressibleMsgBox(CustomMessage('WindowsVersionUnknown'), mbError, MB_OK, IDOK);
    Result := False;
    Exit;
  end;

  Build := VersionLS shr 16;
  Log(Format('Windows kernel file version: %d.%d.%d', [VersionMS shr 16, VersionMS and $FFFF, Build]));
  Result := (VersionMS > MinimumWindowsMajorMinor) or
    ((VersionMS = MinimumWindowsMajorMinor) and (Build >= MinimumWindowsBuild));
  if not Result then
    SuppressibleMsgBox(CustomMessage('WindowsVersionRequired'), mbError, MB_OK, IDOK);
end;

function OpenExistingFile(FileName: String; DesiredAccess, ShareMode,
  SecurityAttributes, CreationDisposition, FlagsAndAttributes: Cardinal;
  TemplateFile: THandle): THandle;
  external 'CreateFileW@kernel32.dll stdcall';
function CloseFileHandle(FileHandle: THandle): Boolean;
  external 'CloseHandle@kernel32.dll stdcall';

function CanReplaceFile(const FileName: String): Boolean;
var
  FileHandle: THandle;
begin
  Result := not FileExists(FileName);
  if Result then
    Exit;
  FileHandle := OpenExistingFile(FileName, GenericWrite, ShareReadWriteDelete,
    0, OpenExisting, FileAttributeNormal, 0);
  Result := FileHandle <> InvalidHandleValue;
  if Result then
    CloseFileHandle(FileHandle);
end;

function LockedFile(const AppDir: String): String;
begin
  Result := AppDir + '\Telegram.exe';
  if not CanReplaceFile(Result) then
    Exit;
  Result := AppDir + '\Updater.exe';
  if not CanReplaceFile(Result) then
    Exit;
  Result := AppDir + '\modules\x64\d3d\d3dcompiler_47.dll';
  if CanReplaceFile(Result) then
    Result := '';
end;

function IsRegisteredLocation(const AppDir: String): Boolean;
var
  RegisteredDir: String;
begin
  Result := RegQueryStringValue(HKCU64, UninstallKey, 'InstallLocation', RegisteredDir) and
    (CompareText(AddBackslash(AppDir), AddBackslash(RegisteredDir)) = 0);
end;

function UpgradingLegacyInstall: Boolean;
var
  RegisteredName: String;
begin
  Result := IsRegisteredLocation(ExpandConstant('{app}')) and
    RegQueryStringValue(HKCU64, UninstallKey, 'DisplayName', RegisteredName) and
    (CompareText(RegisteredName, '{#LegacyAppName}') = 0);
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
var
  AppDir, BlockedFile: String;
  Attempt, ExitCode: Integer;
begin
  Result := '';
  AppDir := ExpandConstant('{app}');
  BlockedFile := LockedFile(AppDir);
  if BlockedFile = '' then
    Exit;

  if DirExists(AppDir + '\TelegramForcePortable') and
    FileExists(AppDir + '\Telegram.exe') and
    IsRegisteredLocation(AppDir) then
  begin
    Log('Asking this installed copy of Telegram to exit before replacing its files.');
    if not Exec(AppDir + '\Telegram.exe', '-quit -noupdate', AppDir,
      SW_HIDE, ewNoWait, ExitCode) then
      Log('Could not start the Telegram quit helper: ' + SysErrorMessage(ExitCode));
  end;

  for Attempt := 1 to 40 do
  begin
    Sleep(250);
    BlockedFile := LockedFile(AppDir);
    if BlockedFile = '' then
      Exit;
  end;
  Result := FmtMessage(CustomMessage('FileNotReplaceable'), [BlockedFile]);
  Log(Result);
end;
