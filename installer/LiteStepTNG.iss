; ==============================================================================
;  LiteStep TNG installer (Inno Setup 6)
;
;  Not run by hand: tools\package.ps1 stages the files and calls ISCC with
;      /DAppVersion=  /DAppNumericVersion=  /DStageDir=  /DOutputDir=  /DRepoUrl=
;  This script only packs the staged folder; what ships is decided in package.ps1.
;
;  LiteStep is installed per user and runs ON TOP of Explorer ("-overlay"):
;  Explorer stays the Windows shell, nothing is changed in Winlogon, and no
;  administrator rights are needed. The folder must be writable by the user,
;  because LiteStep keeps the theme selection, personal.rc and the favourites
;  next to its executable.
; ==============================================================================

#ifndef AppVersion
  #define AppVersion "0.0.0-dev"
#endif
#ifndef AppNumericVersion
  #define AppNumericVersion "0.0.0"
#endif
#ifndef StageDir
  #define StageDir "..\dist\stage"
#endif
#ifndef OutputDir
  #define OutputDir "..\dist"
#endif
#ifndef RepoUrl
  #define RepoUrl ""
#endif

[Setup]
AppId={{6F0B3C2E-8D41-4E7A-9C55-2B7A1E4D9F30}
AppName=LiteStep TNG
AppVersion={#AppVersion}
AppVerName=LiteStep TNG {#AppVersion}
VersionInfoVersion={#AppNumericVersion}
AppPublisher=LiteStep TNG contributors
#if RepoUrl != ""
AppPublisherURL={#RepoUrl}
AppSupportURL={#RepoUrl}/issues
AppUpdatesURL={#RepoUrl}/releases
#endif
DefaultDirName={localappdata}\Programs\LiteStepTNG
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.19041
OutputDir={#OutputDir}
OutputBaseFilename=LiteStepTNG-{#AppVersion}-x64-setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
UninstallDisplayIcon={app}\litestep.exe
UninstallDisplayName=LiteStep TNG
; LiteStep is closed by this script (see [Code]); the Restart Manager would only
; see a window-less shell process it cannot close politely.
CloseApplications=no

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "spanish"; MessagesFile: "compiler:Languages\Spanish.isl"

[CustomMessages]
english.AutoStart=Start LiteStep when I sign in to Windows
spanish.AutoStart=Iniciar LiteStep al iniciar sesión en Windows
english.RunNow=Start LiteStep now
spanish.RunNow=Iniciar LiteStep ahora
english.ShortcutComment=LiteStep running on top of Explorer
spanish.ShortcutComment=LiteStep sobre el Explorador de Windows
english.FontNote=The LiteStep Enhanced theme is designed for the font "hooge 05_55", which is not included. Without it Windows substitutes another font and the theme's text will look different.
spanish.FontNote=El tema LiteStep Enhanced está diseñado para la fuente "hooge 05_55", que no se incluye. Sin ella Windows usa otra fuente y el texto del tema se verá distinto.

[Tasks]
Name: "autostart"; Description: "{cm:AutoStart}"

[Files]
; Everything staged, except the theme selection, which is the user's once installed.
Source: "{#StageDir}\*"; DestDir: "{app}"; Excludes: "theme-select.rc"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#StageDir}\theme-select.rc"; DestDir: "{app}"; Flags: onlyifdoesntexist uninsneveruninstall

[Icons]
Name: "{autoprograms}\LiteStep TNG"; Filename: "{app}\litestep.exe"; Parameters: "-overlay"; WorkingDir: "{app}"; Comment: "{cm:ShortcutComment}"

[Registry]
; Explorer is the shell; LiteStep is just a program started at sign-in.
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; ValueName: "LiteStep"; ValueData: """{app}\litestep.exe"" -overlay"; Tasks: autostart; Flags: uninsdeletevalue

[Run]
Filename: "{app}\litestep.exe"; Parameters: "-overlay"; WorkingDir: "{app}"; Description: "{cm:RunNow}"; Flags: nowait postinstall skipifsilent

[Code]
// LiteStep hides Explorer's taskbar and desktop icons while it runs and puts them
// back when it unloads. Killing it skips that and leaves the user without a
// taskbar, so it is asked to quit first ("!Quit") and only killed if it has not
// gone after a few seconds.
//
// Only the copy installed HERE is touched: another LiteStep running from a
// different folder is none of this installer's business. The check is on the
// path of the running process, which PowerShell can see and Inno cannot.
procedure StopLiteStep();
var
  ResultCode: Integer;
  Executable, Script: String;
begin
  Executable := ExpandConstant('{app}\litestep.exe');
  if not FileExists(Executable) then
    Exit;

  StringChangeEx(Executable, '''', '''''', True);
  Script :=
    '$exe = ''' + Executable + '''; ' +
    '$own = Get-Process litestep -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq $exe }; ' +
    'if ($own) { ' +
      'Start-Process $exe -ArgumentList ''!Quit''; ' +
      '$own | Wait-Process -Timeout 8 -ErrorAction SilentlyContinue; ' +
      '$own | Where-Object { -not $_.HasExited } | Stop-Process -Force -ErrorAction SilentlyContinue ' +
    '}';
  Exec(ExpandConstant('{sys}\WindowsPowerShell\v1.0\powershell.exe'),
    '-NoProfile -NonInteractive -ExecutionPolicy Bypass -Command "' + Script + '"',
    '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  StopLiteStep();
  Result := '';
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usUninstall then
    StopLiteStep();
end;

procedure InitializeWizard();
begin
  WizardForm.TasksList.Hint := CustomMessage('FontNote');
end;

procedure CurPageChanged(CurPageID: Integer);
begin
  if CurPageID = wpFinished then
    WizardForm.FinishedLabel.Caption := WizardForm.FinishedLabel.Caption + #13#10 + #13#10 + CustomMessage('FontNote');
end;
