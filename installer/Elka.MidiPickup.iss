#ifndef ReleaseVersion
  #error ReleaseVersion is required.
#endif
#ifndef VersionNumbers
  #error VersionNumbers is required.
#endif
#ifndef SourceDir
  #error SourceDir must be a clean portable staging folder.
#endif
#ifndef OutputDir
  #error OutputDir is required.
#endif
#if Ver < EncodeVer(6, 6, 0)
  #error Inno Setup 6.6 or newer is required.
#endif
#define AppName "Elka MIDI Pickup"
#define ExeName "Elka.MidiPickup.exe"
#define RepoURL "https://github.com/torment78/Elka.MidiPickup"

[Setup]
#ifdef InstallerTest
AppId=ElkaMidiPickupInstallerSmokeTest
AppMutex=Local\ElkaMidiPickupInstallerSmokeTest
OutputBaseFilename=Elka.MidiPickup-{#ReleaseVersion}-installer-smoketest
#else
AppId={{644AA5D9-15A7-49E4-BFB3-3C5BBE3873F7}
AppMutex=Local\ElkaMidiPickupTestApp
OutputBaseFilename=Elka.MidiPickup-{#ReleaseVersion}-setup-win-x64-unsigned
#endif
AppName={#AppName}
AppVersion={#ReleaseVersion}
AppVerName={#AppName} {#ReleaseVersion} — Preview
AppPublisher=ElkaSoft
AppPublisherURL={#RepoURL}
AppSupportURL={#RepoURL}/issues
AppUpdatesURL={#RepoURL}/releases
VersionInfoVersion={#VersionNumbers}
VersionInfoProductName={#AppName}
VersionInfoDescription={#AppName} preview installer (unsigned)
DefaultDirName={autopf}\ElkaSoft\{#AppName}
DefaultGroupName=ElkaSoft
DisableDirPage=no
DisableProgramGroupPage=yes
DisableWelcomePage=no
UsePreviousAppDir=yes
UsePreviousTasks=yes
OutputDir={#OutputDir}
SetupIconFile=..\app\Assets\Elka.MidiPickup.ico
WizardImageFile=..\docs\images\midi-pickup-vertical.png
WizardSmallImageFile=..\app\Assets\Elka.MidiPickup.png
WizardStyle=modern dark polar includetitlebar
WizardSizePercent=120,120
Compression=lzma2/ultra64
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.22000
PrivilegesRequired=admin
PrivilegesRequiredOverridesAllowed=commandline
UninstallDisplayIcon={app}\{#ExeName}
UninstallDisplayName={#AppName} (Preview)
CloseApplications=yes
CloseApplicationsFilter={#ExeName}
RestartApplications=no
SignedUninstaller=no
InfoBeforeFile=BeforeInstall.txt

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
; Explicit payload allowlist: no profiles, logs, test binaries, or build caches.
Source: "{#SourceDir}\{#ExeName}"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourceDir}\README.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourceDir}\docs\*.md"; DestDir: "{app}\docs"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#SourceDir}\docs\images\*.png"; DestDir: "{app}\docs\images"; Flags: ignoreversion
Source: "{#SourceDir}\docs\images\*.jpg"; DestDir: "{app}\docs\images"; Flags: ignoreversion
Source: "{#SourceDir}\app\Assets\*.ico"; DestDir: "{app}\app\Assets"; Flags: ignoreversion
Source: "{#SourceDir}\app\Assets\*.png"; DestDir: "{app}\app\Assets"; Flags: ignoreversion
Source: "Elka.MidiPickup.installed"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\ElkaSoft\{#AppName}"; Filename: "{app}\{#ExeName}"; Parameters: "--show"; WorkingDir: "{app}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#ExeName}"; Parameters: "--show"; WorkingDir: "{app}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#ExeName}"; Parameters: "--show"; Description: "Launch {#AppName}"; Flags: nowait postinstall skipifsilent runasoriginaluser

[UninstallDelete]
; Saved settings and unrelated files are deliberately retained.
Type: dirifempty; Name: "{app}"

[Code]
procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  Command: String;
begin
  if CurUninstallStep = usUninstall then
    if RegQueryStringValue(HKCU, 'Software\Microsoft\Windows\CurrentVersion\Run', 'ElkaMidiPickup', Command) then
      if SameText(Command, '"' + ExpandConstant('{app}\{#ExeName}') + '"') then
        RegDeleteValue(HKCU, 'Software\Microsoft\Windows\CurrentVersion\Run', 'ElkaMidiPickup');
end;
