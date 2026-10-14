; Inno Setup script for Fairfax. Built by bin/build-windows.ps1, which passes
; AppVersion, SourceDir (the deployed folder) and OutputDir.
#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef SourceDir
  #define SourceDir "..\..\build-windows\Fairfax"
#endif
#ifndef OutputDir
  #define OutputDir "..\..\build-windows"
#endif

[Setup]
AppId={{6A3F0D52-8B1E-4C57-9F2A-5E0B7C41D9A3}
AppName=Fairfax
AppVersion={#AppVersion}
AppPublisher=Fairfax
DefaultDirName={autopf}\Fairfax
DefaultGroupName=Fairfax
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir={#OutputDir}
OutputBaseFilename=Fairfax-{#AppVersion}-setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
UninstallDisplayIcon={app}\fairfax.exe
#ifdef SetupIcon
SetupIconFile={#SetupIcon}
#endif
LicenseFile={#SourceDir}\LICENSE.txt
; PATH changes need a signal to running shells; Inno broadcasts it itself.
ChangesEnvironment=yes
CloseApplications=yes

[Tasks]
Name: "startup"; Description: "Start Fairfax when I sign in (needed for alarms)"; GroupDescription: "Options:"; Flags: unchecked
Name: "addpath"; Description: "Add the frfx command to PATH"; GroupDescription: "Options:"; Flags: unchecked
Name: "quickkey"; Description: "Create a Start-menu shortcut for Quick Entry (assign a hotkey in its properties)"; GroupDescription: "Options:"; Flags: unchecked

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\Fairfax"; Filename: "{app}\fairfax.exe"
Name: "{group}\Fairfax Quick Entry"; Filename: "{app}\fairfax.exe"; Parameters: "--quick"; Tasks: quickkey
Name: "{userstartup}\Fairfax"; Filename: "{app}\fairfax.exe"; Parameters: "--background"; Tasks: startup

[Registry]
; fairfax:// links, for this user only. Both keys are removed on uninstall.
Root: HKCU; Subkey: "Software\Classes\fairfax"; ValueType: string; ValueData: "URL:Fairfax"; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\fairfax"; ValueType: string; ValueName: "URL Protocol"; ValueData: ""
Root: HKCU; Subkey: "Software\Classes\fairfax\DefaultIcon"; ValueType: string; ValueData: "{app}\fairfax.exe,0"
Root: HKCU; Subkey: "Software\Classes\fairfax\shell\open\command"; ValueType: string; ValueData: """{app}\fairfax.exe"" ""%1"""
; frfx on the user's PATH, when chosen. Removed again on uninstall. Two
; entries so an empty PATH value gets {app} alone, with no leading semicolon.
Root: HKCU; Subkey: "Environment"; ValueType: expandsz; ValueName: "Path"; ValueData: "{app}"; Tasks: addpath; Check: AddPathPlain(ExpandConstant('{app}')); Flags: uninsdeletevalue
Root: HKCU; Subkey: "Environment"; ValueType: expandsz; ValueName: "Path"; ValueData: "{olddata};{app}"; Tasks: addpath; Check: AddPathAppend(ExpandConstant('{app}'))

[Run]
Filename: "{app}\fairfax.exe"; Description: "Start Fairfax"; Flags: nowait postinstall skipifsilent

[Code]
function NeedsPathEntry(Dir: string): Boolean;
var
  Path: string;
begin
  if not RegQueryStringValue(HKCU, 'Environment', 'Path', Path) then
    Path := '';
  Result := Pos(';' + Lowercase(Dir) + ';', ';' + Lowercase(Path) + ';') = 0;
end;

// No PATH value, or it is empty: write {app} on its own.
function AddPathPlain(Dir: string): Boolean;
var
  Path: string;
begin
  Result := (not RegQueryStringValue(HKCU, 'Environment', 'Path', Path)) or (Path = '');
  Result := Result and NeedsPathEntry(Dir);
end;

// PATH already has something: append {app} to it.
function AddPathAppend(Dir: string): Boolean;
var
  Path: string;
begin
  Result := RegQueryStringValue(HKCU, 'Environment', 'Path', Path) and (Path <> '');
  Result := Result and NeedsPathEntry(Dir);
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  Path, Dir: string;
  P: Integer;
begin
  if CurUninstallStep <> usPostUninstall then
    Exit;
  Dir := ExpandConstant('{app}');
  if RegQueryStringValue(HKCU, 'Environment', 'Path', Path) then
  begin
    P := Pos(';' + Lowercase(Dir), Lowercase(Path));
    if P > 0 then
    begin
      Delete(Path, P, Length(Dir) + 1);
      RegWriteExpandStringValue(HKCU, 'Environment', 'Path', Path);
    end;
  end;
end;
