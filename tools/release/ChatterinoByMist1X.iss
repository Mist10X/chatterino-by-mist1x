; Installer for Chatterino by Mist1X (built by build-release.ps1).
;
; The program keeps its settings, cache and logs next to itself (portable
; profile), so it is installed for the current user into a folder that user can
; write to: no administrator rights, no Program Files. Updating over an existing
; installation keeps the settings; uninstalling asks whether to remove them.

#ifndef AppVersion
#define AppVersion "1.0.0"
#endif
#ifndef StagingDir
#define StagingDir "staging"
#endif
#ifndef OutputDir
#define OutputDir "dist"
#endif

#define AppName "Chatterino by Mist1X"
#define AppExe "Leafyrino.exe"
; Must match the AppUserModelID the program sets (src/common/Version.cpp),
; so the shortcut and the running window share one taskbar button.
#define AppUserModel "Mist1X.ChatterinoByMist1X"

[Setup]
AppId={{0599F313-9171-4ABA-9366-C234F95C7018}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher=Mist1X
AppCopyright=Chatterino by Mist1X — на основе Leafyrino, Chatterino7 и Chatterino 2 (MIT)
VersionInfoVersion={#AppVersion}
VersionInfoProductName={#AppName}
VersionInfoDescription=Установщик {#AppName}
DefaultDirName={localappdata}\Programs\{#AppName}
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir={#OutputDir}
OutputBaseFilename=ChatterinoByMist1X-Setup-{#AppVersion}
SetupIconFile={#StagingDir}\Chatterino-by-Mist1X.ico
UninstallDisplayIcon={app}\{#AppExe}
UninstallDisplayName={#AppName}
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
; Close a running copy before replacing its files (it is in the same folder).
CloseApplications=yes
RestartApplications=no
LicenseFile={#StagingDir}\LICENSE.txt
InfoAfterFile={#StagingDir}\Что нового.txt
RestartIfNeededByRun=no

[Languages]
Name: "russian"; MessagesFile: "compiler:Languages\Russian.isl"

[Tasks]
Name: "desktopicon"; Description: "Создать значок на рабочем столе"; GroupDescription: "Дополнительно:"

[Files]
Source: "{#StagingDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\{#AppName}"; Filename: "{app}\{#AppExe}"; AppUserModelID: "{#AppUserModel}"
Name: "{autoprograms}\{#AppName} — руководство"; Filename: "{app}\Руководство.html"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExe}"; AppUserModelID: "{#AppUserModel}"; Tasks: desktopicon

[Run]
Filename: "{app}\Руководство.html"; Description: "Открыть руководство (как пользоваться)"; Flags: postinstall shellexec skipifsilent nowait
Filename: "{app}\{#AppExe}"; Description: "Запустить {#AppName}"; Flags: postinstall nowait skipifsilent
; Started from the program's own update (/AUTOUPDATE=1): start it again.
Filename: "{app}\{#AppExe}"; Flags: nowait; Check: IsAutoUpdate

[UninstallDelete]
; Files the program itself creates next to it that are not settings.
Type: filesandordirs; Name: "{app}\Cache"
Type: filesandordirs; Name: "{app}\Crashes"
Type: filesandordirs; Name: "{app}\IPC"

[Code]
function IsAutoUpdate: Boolean;
begin
  Result := ExpandConstant('{param:AUTOUPDATE|0}') = '1';
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  Dir: String;
begin
  if CurUninstallStep <> usPostUninstall then
    exit;
  Dir := ExpandConstant('{app}');
  if UninstallSilent then
    exit;
  if MsgBox('Удалить также ваши настройки, вход в Twitch, журналы и аватарки?' + #13#10 + #13#10 +
            'Нажмите «Нет», если собираетесь установить программу снова и хотите сохранить настройки.',
            mbConfirmation, MB_YESNO or MB_DEFBUTTON2) = IDYES then
  begin
    DelTree(Dir + '\Settings', True, True, True);
    DelTree(Dir + '\Logs', True, True, True);
    DelTree(Dir + '\Misc', True, True, True);
    DelTree(Dir + '\ProfileAvatars', True, True, True);
    DelTree(Dir + '\Plugins', True, True, True);
    DelTree(Dir + '\Themes', True, True, True);
    RemoveDir(Dir);
  end;
end;
