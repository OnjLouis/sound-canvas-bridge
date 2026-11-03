; The wizard owns shortcuts/uninstall registration. The signed maintenance
; worker owns both audio components and retains rollback until wizard success.
#ifndef BridgeVersion
  #error BridgeVersion is required
#endif
#ifndef PayloadWorker
  #error PayloadWorker is required
#endif
#ifndef MaintenanceWorker
  #error MaintenanceWorker is required
#endif
#ifndef InstallerOutput
  #error InstallerOutput is required
#endif

[Setup]
AppId=OnjMusic.SoundCanvasBridge
AppName=Sound Canvas Bridge
AppVersion={#BridgeVersion}
AppPublisher=OnjMusic
AppPublisherURL=https://github.com/OnjLouis/sound-canvas-bridge
AppSupportURL=https://github.com/OnjLouis/sound-canvas-bridge/issues
AppUpdatesURL=https://github.com/OnjLouis/sound-canvas-bridge/releases
DefaultDirName={autopf}\OnjMusic\Sound Canvas Bridge Setup
DisableDirPage=yes
DisableProgramGroupPage=yes
DisableWelcomePage=no
DefaultGroupName=OnjMusic
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
WizardStyle=modern
OutputDir={#InstallerOutput}
OutputBaseFilename=SoundCanvasBridge-{#BridgeVersion}-windows-x64
VersionInfoVersion={#BridgeVersion}
VersionInfoProductName=Sound Canvas Bridge
VersionInfoProductVersion={#BridgeVersion}
Compression=lzma2
SolidCompression=yes
CloseApplications=no
RestartApplications=no
SetupMutex=OnjMusicSoundCanvasBridgeWizard
UninstallDisplayIcon={autopf}\OnjMusic\Sound Canvas Bridge\Sound Canvas Bridge.exe
UninstallDisplayName=Sound Canvas Bridge
SetupLogging=yes

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Messages]
WelcomeLabel2=This will install Sound Canvas Bridge {#BridgeVersion}, including the standalone app and VST3 plug-in.%n%nBefore installing, stop MIDI playback and unload the bridge from audio hosts. Setup can close the standalone for you. Your settings and NKS presets will be kept.%n%nClick Next to continue, or Cancel to exit Setup.

[Files]
Source: "{#PayloadWorker}"; DestName: "PayloadInstaller.exe"; Flags: dontcopy
Source: "{#MaintenanceWorker}"; DestDir: "{app}"; DestName: "SoundCanvasMaintenance.exe"; Flags: ignoreversion

[Icons]
Name: "{commonprograms}\OnjMusic\Sound Canvas Bridge"; Filename: "{autopf}\OnjMusic\Sound Canvas Bridge\Sound Canvas Bridge.exe"; Parameters: "--show-controls"; Check: WantStartMenu
Name: "{commondesktop}\Sound Canvas Bridge"; Filename: "{autopf}\OnjMusic\Sound Canvas Bridge\Sound Canvas Bridge.exe"; Parameters: "--show-controls"; Check: WantDesktop

[Run]
Filename: "{app}\SoundCanvasMaintenance.exe"; Parameters: "--migrate-shortcuts"; Flags: runhidden runasoriginaluser

[Code]
var
  Worker: String;
  Pending: Boolean;
  FinalizeIssue: Boolean;
  ShortcutPage: TWizardPage;
  DesktopCheck, StartMenuCheck, OpenCheck: TNewCheckBox;

function GetCurrentProcessId: LongWord;
  external 'GetCurrentProcessId@kernel32.dll stdcall';

function ShortcutChoice(const Parameter, Previous: String): Boolean;
var
  Value: String;
begin
  Value := ExpandConstant('{param:' + Parameter + '|' + GetPreviousData(Previous, '1') + '}');
  if (Value <> '0') and (Value <> '1') then
    RaiseException(Parameter + ' must be 0 or 1.');
  Result := Value = '1';
end;

procedure InitializeWizard;
begin
  WizardForm.ReadyMemo.WordWrap := True;
  WizardForm.ReadyMemo.ScrollBars := ssVertical;
  { Native checkboxes expose names and checked states without the owner-drawn
    checklist's literal ampersands and numeric accessibility values. }
  ShortcutPage := CreateCustomPage(wpWelcome, 'Shortcuts', 'Choose where to add Sound Canvas Bridge.');
  StartMenuCheck := TNewCheckBox.Create(WizardForm);
  StartMenuCheck.Parent := ShortcutPage.Surface;
  StartMenuCheck.Caption := 'Add to Start menu';
  StartMenuCheck.SetBounds(0, ScaleY(8), ShortcutPage.SurfaceWidth, ScaleY(24));
  StartMenuCheck.Checked := ShortcutChoice('STARTMENU', 'StartMenuShortcut');
  DesktopCheck := TNewCheckBox.Create(WizardForm);
  DesktopCheck.Parent := ShortcutPage.Surface;
  DesktopCheck.Caption := 'Add a desktop shortcut';
  DesktopCheck.SetBounds(0, ScaleY(40), ShortcutPage.SurfaceWidth, ScaleY(24));
  DesktopCheck.Checked := ShortcutChoice('DESKTOP', 'DesktopShortcut');
  OpenCheck := TNewCheckBox.Create(WizardForm);
  OpenCheck.Parent := WizardForm.FinishedPage;
  OpenCheck.Caption := 'Open Sound Canvas Bridge';
  OpenCheck.SetBounds(WizardForm.FinishedLabel.Left, WizardForm.FinishedLabel.Top + WizardForm.FinishedLabel.Height + ScaleY(16),
    WizardForm.FinishedLabel.Width, ScaleY(24));
  OpenCheck.Checked := True;
end;

function WantStartMenu: Boolean;
begin
  Result := StartMenuCheck.Checked;
end;

function WantDesktop: Boolean;
begin
  Result := DesktopCheck.Checked;
end;

procedure RegisterPreviousData(PreviousDataKey: Integer);
begin
  if WantStartMenu then SetPreviousData(PreviousDataKey, 'StartMenuShortcut', '1')
  else SetPreviousData(PreviousDataKey, 'StartMenuShortcut', '0');
  if WantDesktop then SetPreviousData(PreviousDataKey, 'DesktopShortcut', '1')
  else SetPreviousData(PreviousDataKey, 'DesktopShortcut', '0');
end;

function RunWorker(const Executable, Arguments: String): Boolean;
var
  ExitCode: Integer;
begin
  Result := Exec(Executable, Arguments, '', SW_HIDE, ewWaitUntilTerminated, ExitCode);
  Result := Result and (ExitCode = 0);
end;

function UpdateReadyMemo(Space, NewLine, MemoUserInfoInfo, MemoDirInfo,
  MemoTypeInfo, MemoComponentsInfo, MemoGroupInfo, MemoTasksInfo: String): String;
begin
  Result := 'Standalone: ' + ExpandConstant('{autopf}\OnjMusic\Sound Canvas Bridge') + NewLine +
    'VST3: ' + ExpandConstant('{commoncf}\VST3\Sound Canvas Bridge.vst3') + NewLine + NewLine +
    'Installed Apps: Sound Canvas Bridge' + NewLine + NewLine;
  if WantStartMenu then Result := Result + 'Start-menu shortcut' + NewLine;
  if WantDesktop then Result := Result + 'Desktop shortcut' + NewLine;
  Result := Result + NewLine +
    'Stop MIDI playback and unload the bridge from audio hosts before continuing. ' +
    'Setup closes the standalone app, but never closes your audio hosts. ' +
    'Your audio/MIDI settings, NKS presets and original Roland installation are kept.';
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  Result := '';
  ExtractTemporaryFile('PayloadInstaller.exe');
  Worker := ExpandConstant('{tmp}\PayloadInstaller.exe');
  Pending := True;
  if not RunWorker(Worker, '--setup-apply ' + IntToStr(GetCurrentProcessId)) then
    Result := 'Sound Canvas Bridge could not be installed. Previous files were retained or restored. ' +
      'Unload the bridge from audio hosts and try again. Details are in ' +
      ExpandConstant('{commonappdata}\OnjMusicSoundCanvasBridge-Installer\Installer.log')
  else if not RunWorker(Worker, '--setup-seal ' + IntToStr(GetCurrentProcessId)) then
    Result := 'Installation verification failed. Previous components will be restored.';
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
  ErrorCode: Integer;
begin
  if (CurStep = ssPostInstall) and Pending then begin
    { Only housekeeping remains. Do not undo an installed product if a backup
      is temporarily locked; the durable committed journal permits recovery. }
    Pending := False;
    if not RunWorker(Worker, '--setup-commit ' + IntToStr(GetCurrentProcessId)) then begin
      FinalizeIssue := True;
      Log('Installation succeeded; recovery cleanup was retained for the next maintenance run.');
      WizardForm.FinishedLabel.Caption := 'The bridge files were installed, but recovery finalization could not finish. Run setup again before using the bridge. Your settings and presets were kept. Details are in ' +
        ExpandConstant('{commonappdata}\OnjMusicSoundCanvasBridge-Installer\Installer.log');
    end;
  end;
  if (CurStep = ssDone) and not WizardSilent and not FinalizeIssue and OpenCheck.Checked then
    if not ExecAsOriginalUser(ExpandConstant('{autopf}\OnjMusic\Sound Canvas Bridge\Sound Canvas Bridge.exe'), '', '',
      SW_SHOWNORMAL, ewNoWait, ErrorCode) then
      SuppressibleMsgBox('Sound Canvas Bridge was installed, but could not be opened. Use its shortcut or Start-menu entry.', mbError, MB_OK, IDOK);
end;

procedure CurPageChanged(CurPageID: Integer);
begin
  if CurPageID = ShortcutPage.ID then WizardForm.ActiveControl := StartMenuCheck;
  if CurPageID = wpFinished then begin
    OpenCheck.Visible := not FinalizeIssue;
    if OpenCheck.Visible then WizardForm.ActiveControl := OpenCheck;
  end;
end;

function GetCustomSetupExitCode: Integer;
begin
  if FinalizeIssue then Result := 1 else Result := 0;
end;

procedure DeinitializeSetup;
begin
  if Pending and (Worker <> '') then
    if not RunWorker(Worker, '--setup-rollback ' + IntToStr(GetCurrentProcessId)) then
      Log('Rollback could not finish. Protected recovery files have been retained.');
end;

function InitializeUninstall: Boolean;
begin
  Result := True;
end;

function UninstallStepMessage: String;
begin
  Result := 'The bridge could not be removed. Unload it from audio hosts and try again. ' +
    'Your settings and NKS presets have not been deleted. Details are in ' +
    ExpandConstant('{commonappdata}\OnjMusicSoundCanvasBridge-Installer\Installer.log');
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usUninstall then
    if not RunWorker(ExpandConstant('{app}\SoundCanvasMaintenance.exe'), '--uninstall') then
      RaiseException(UninstallStepMessage);
end;
