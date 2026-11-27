; ==============================================================================
;  LiteStep TNG installer (Inno Setup 6)
;
;  Not run by hand: tools\package.ps1 stages the files and calls ISCC with
;      /DAppVersion=  /DAppNumericVersion=  /DStageDir=  /DOutputDir=  /DRepoUrl=
;  This script only packs the staged folder; what ships is decided in package.ps1.
;
;  LiteStep is installed per user, with no administrator rights. By default it
;  runs ON TOP of Explorer, which stays the Windows shell. On Windows 11 the
;  optional task "shell" makes it this user's shell instead (HKCU Winlogon
;  "Shell", the same value tools\set-shell.ps1 writes): LiteStep then starts
;  Windows' ShellAppRuntime.exe, which Store apps and Settings need. Uninstalling
;  puts Explorer back, but only if the value still names this copy.
;  The folder must be writable by the user, because LiteStep keeps the theme
;  selection, personal.rc and the favourites next to its executable.
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
english.UseAsShell=Use LiteStep as the Windows shell instead of Explorer (from the next sign-in)
spanish.UseAsShell=Usar LiteStep como shell de Windows en lugar del Explorador (desde el próximo inicio de sesión)
english.ShellNote=LiteStep will be your shell from the next sign-in. To go back to Explorer, uninstall LiteStep TNG, or quit LiteStep from its menu. If the desktop ever comes up empty: Ctrl+Alt+Del, Task Manager, Run new task, explorer.exe, then uninstall.
spanish.ShellNote=LiteStep será tu shell desde el próximo inicio de sesión. Para volver al Explorador, desinstala LiteStep TNG o cierra LiteStep desde su menú. Si alguna vez el escritorio aparece vacío: Ctrl+Alt+Supr, Administrador de tareas, Ejecutar nueva tarea, explorer.exe, y luego desinstala.
; FontNote is written by tools\package.ps1 from the themes' theme.json (requires.fonts).
#ifdef FontNoteFile
#include FontNoteFile
#else
english.FontNote=
spanish.FontNote=
#endif

[Tasks]
Name: "autostart"; Description: "{cm:AutoStart}"
; Windows 11 only: without ShellAppRuntime.exe, Store apps and Settings would not show.
Name: "shell"; Description: "{cm:UseAsShell}"; Flags: unchecked; Check: HasShellAppRuntime

[Files]
; Everything staged, except the theme selection, which is the user's once installed.
Source: "{#StageDir}\*"; DestDir: "{app}"; Excludes: "theme-select.rc"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#StageDir}\theme-select.rc"; DestDir: "{app}"; Flags: onlyifdoesntexist uninsneveruninstall

[Icons]
Name: "{autoprograms}\LiteStep TNG"; Filename: "{app}\litestep.exe"; WorkingDir: "{app}"; Comment: "{cm:ShortcutComment}"

[Registry]
; Explorer is the shell; LiteStep is just a program started at sign-in.
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; ValueName: "LiteStep"; ValueData: """{app}\litestep.exe"" -autostart"; Tasks: autostart; Flags: uninsdeletevalue
; LiteStep as this user's shell. Not uninsdeletevalue: the uninstaller removes it only if
; it still names this copy (see RemoveOwnShell), never a shell the user chose since.
Root: HKCU; Subkey: "Software\Microsoft\Windows NT\CurrentVersion\Winlogon"; ValueType: string; ValueName: "Shell"; ValueData: """{app}\litestep.exe"""; Tasks: shell

[Run]
Filename: "{app}\litestep.exe"; WorkingDir: "{app}"; Description: "{cm:RunNow}"; Flags: nowait postinstall skipifsilent

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

const
  WinlogonKey = 'Software\Microsoft\Windows NT\CurrentVersion\Winlogon';

var
  ShellTaskPreset: Boolean;  // the shell task was ticked for an existing shell once

function HasShellAppRuntime(): Boolean;
begin
  Result := FileExists(ExpandConstant('{sys}\ShellAppRuntime.exe'));
end;

// True when this user's Winlogon "Shell" names the litestep.exe of this install,
// quoted or not, in any case.
function ShellIsThisCopy(): Boolean;
var
  Value: String;
begin
  Result := False;
  if RegQueryStringValue(HKCU, WinlogonKey, 'Shell', Value) then
  begin
    StringChangeEx(Value, '"', '', True);
    Result := CompareText(Trim(Value), ExpandConstant('{app}\litestep.exe')) = 0;
  end;
end;

// Explorer comes back from the next sign-in: without the value, the machine-wide
// default (explorer.exe) applies.
procedure RemoveOwnShell();
begin
  if ShellIsThisCopy() then
    RegDeleteValue(HKCU, WinlogonKey, 'Shell');
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  StopLiteStep();
  Result := '';
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usUninstall then
  begin
    StopLiteStep();
    RemoveOwnShell();
  end;
end;

// Updating with the shell task cleared turns LiteStep back into a program on top of
// Explorer; a shell value that names another program is left alone.
procedure CurStepChanged(CurStep: TSetupStep);
begin
  if (CurStep = ssPostInstall) and not WizardIsTaskSelected('shell') then
    RemoveOwnShell();
end;

procedure InitializeWizard();
begin
  if CustomMessage('FontNote') <> '' then
    WizardForm.TasksList.Hint := CustomMessage('FontNote');
end;

procedure CurPageChanged(CurPageID: Integer);
begin
  // A copy that is already the shell (set by an earlier install or by set-shell.ps1)
  // shows the task ticked, so "Next" does not quietly hand the session back to Explorer.
  // Not in a silent install: there /TASKS and /MERGETASKS decide, and this event still runs.
  if (CurPageID = wpSelectTasks) and not ShellTaskPreset and not WizardSilent() then
  begin
    ShellTaskPreset := True;
    if HasShellAppRuntime() and ShellIsThisCopy() then
      WizardSelectTasks('shell');
  end;
  if CurPageID = wpFinished then
  begin
    if WizardIsTaskSelected('shell') then
      WizardForm.FinishedLabel.Caption := WizardForm.FinishedLabel.Caption + #13#10 + #13#10 + CustomMessage('ShellNote');
    if CustomMessage('FontNote') <> '' then
      WizardForm.FinishedLabel.Caption := WizardForm.FinishedLabel.Caption + #13#10 + #13#10 + CustomMessage('FontNote');
  end;
end;
