; The one-file Windows install, built by scripts/make-windows-setup.sh: per user
; (no admin), into %LOCALAPPDATA%\Programs\Journeyman\bin, which goes on the user's PATH.
; ISCC /DVersion=0.0.7 /DNumericVersion=0.0.7 /DSource=<folder with the programs>

#ifndef Version
  #error Pass /DVersion, /DNumericVersion and /DSource (scripts/make-windows-setup.sh does)
#endif

[Setup]
AppId={{6F1C2B7E-5A43-4E0B-9C1D-7A2E8F3B4D10}
AppName=Journeyman
AppVersion={#Version}
VersionInfoVersion={#NumericVersion}
AppPublisher=Journeyman
AppPublisherURL=https://github.com/Jumballaya/Journeyman-Engine
DefaultDirName={autopf}\Journeyman
DisableDirPage=yes
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
ChangesEnvironment=yes
UninstallDisplayIcon={app}\bin\journeyman_editor.exe
WizardStyle=modern
#ifdef Signed
; scripts/make-windows-setup.sh defines the "jm" sign tool when JM_WINDOWS_SIGNTOOL is set.
SignTool=jm
#endif

[Files]
Source: "{#Source}\journeyman_editor.exe"; DestDir: "{app}\bin"; Flags: ignoreversion
Source: "{#Source}\jm.exe"; DestDir: "{app}\bin"; Flags: ignoreversion
Source: "{#Source}\journeyman_engine.exe"; DestDir: "{app}\bin"; Flags: ignoreversion
Source: "{#Source}\journeyman_server.exe"; DestDir: "{app}\bin"; Flags: ignoreversion
Source: "{#Source}\LICENSE"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\Journeyman Editor"; Filename: "{app}\bin\journeyman_editor.exe"

[Run]
Filename: "{app}\bin\journeyman_editor.exe"; Description: "Open Journeyman Editor"; Flags: nowait postinstall skipifsilent

[Code]
const
  EnvKey = 'Environment';

function BinDir: String;
begin
  Result := ExpandConstant('{app}\bin');
end;

// Where BinDir is in the user's PATH, as ';'-wrapped text (0: not there).
function BinInPath(Path: String): Integer;
begin
  Result := Pos(';' + Uppercase(BinDir) + ';', ';' + Uppercase(Path) + ';');
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
  Path: String;
begin
  if CurStep <> ssPostInstall then exit;
  if not RegQueryStringValue(HKCU, EnvKey, 'Path', Path) then Path := '';
  if BinInPath(Path) > 0 then exit;
  if Path <> '' then Path := ';' + Path;
  RegWriteExpandStringValue(HKCU, EnvKey, 'Path', BinDir + Path);
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  Path: String;
  At: Integer;
begin
  if CurUninstallStep <> usUninstall then exit;  // before Inno tells Windows the environment changed
  if not RegQueryStringValue(HKCU, EnvKey, 'Path', Path) then exit;
  At := BinInPath(Path);
  if At = 0 then exit;
  Path := ';' + Path + ';';
  Delete(Path, At, Length(BinDir) + 1);
  RegWriteExpandStringValue(HKCU, EnvKey, 'Path', Copy(Path, 2, Length(Path) - 2));
end;
