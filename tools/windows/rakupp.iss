; Raku++ (rakupp) -- the Windows GUI installer.
;
; Compiled by .github/workflows/release.yml, from the same install layout the
; .zip is packaged from:
;
;   iscc /DAppVersion=3.26.0 "/DLayout=<abs path to dist\rakupp>" tools\windows\rakupp.iss
;
; and gated by t\windows\setup.ps1, which drives the product silently and
; asserts what it did to the machine.
;
; The scripted installer (tools\install-windows.ps1) does the same job without
; a wizard, and the two share a default location -- pick one per machine rather
; than layering them.
;
; NOT SIGNED. Windows SmartScreen will say "unknown publisher" until either a
; code-signing certificate is in the picture or the file earns a reputation.
;
; Needs Inno Setup 6.3+ (for ArchitecturesAllowed=x64compatible).

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef Layout
  #define Layout "..\..\dist\rakupp"
#endif

[Setup]
; NEVER change AppId. It is what an upgrade recognises as "the same product",
; and what Add/Remove Programs keys off; a new one turns every upgrade into a
; second entry sitting beside the first.
AppId={{B0A97D26-9E97-4E71-994E-3A721F59DCBD}
AppName=Raku++
AppVersion={#AppVersion}
AppPublisher=Andrew Shitov
AppPublisherURL=https://raku.online
AppSupportURL=https://github.com/ash/rakupp/issues
AppUpdatesURL=https://github.com/ash/rakupp/releases
VersionInfoVersion={#AppVersion}

; Per user by default: no elevation, no UAC prompt, and a CLI tool on PATH is a
; per-user thing. `/ALLUSERS` (with admin rights) installs into Program Files
; and writes the machine PATH instead -- see EnvRootKey below.
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=commandline
DefaultDirName={autopf}\rakupp
DefaultGroupName=Raku++
DisableProgramGroupPage=yes
LicenseFile=..\..\LICENSE
UninstallDisplayIcon={app}\bin\rakupp.exe
; The engine is x64. x64compatible rather than x64os, so it also installs on
; ARM64 Windows, where it runs emulated -- the same call the scripted installer
; makes.
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
; Makes Setup broadcast WM_SETTINGCHANGE after install AND uninstall, so a new
; Explorer-launched process sees the PATH edit without a sign-out.
ChangesEnvironment=yes
WizardStyle=modern
Compression=lzma2/max
SolidCompression=yes
OutputDir=..\..
OutputBaseFilename=rakupp-setup-windows-x64

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
; The two questions the scripted installer asks, as checkboxes. Both default to
; checked; /TASKS="..." on the command line replaces the selection outright.
Name: "rakualias"; Description: "Also install it under the name &raku"; GroupDescription: "Command names:"
Name: "addtopath";  Description: "Add the &bin folder to my PATH"; GroupDescription: "Environment:"

[Files]
; bin\ carries rakupp.exe and librakupp (the DLL embedders load); lib\ the
; archives --exe links against; include\ the headers it compiles against.
; findRuntime() in src/main.cpp looks for exactly this bin/lib/include shape.
Source: "{#Layout}\bin\*";     DestDir: "{app}\bin";     Flags: ignoreversion
Source: "{#Layout}\lib\*";     DestDir: "{app}\lib";     Flags: ignoreversion recursesubdirs
Source: "{#Layout}\include\*"; DestDir: "{app}\include"; Flags: ignoreversion recursesubdirs
Source: "{#Layout}\share\*";   DestDir: "{app}\share";   Flags: ignoreversion recursesubdirs skipifsourcedoesntexist
Source: "{#Layout}\README.md"; DestDir: "{app}";         Flags: ignoreversion skipifsourcedoesntexist
Source: "{#Layout}\LICENSE";   DestDir: "{app}";         Flags: ignoreversion skipifsourcedoesntexist

; The second name is a COPY here, where install-windows.ps1 makes a hard link:
; a copy is one more [Files] entry, which means Inno owns it for the upgrade,
; the uninstall and a cancelled install alike. It costs ~15 MB on disk and
; almost nothing in the download -- solid compression stores the two identical
; streams once. rakupp resolves its runtime from GetModuleFileNameW, so any
; name inside bin\ is a whole engine.
;
; raku++.exe is NOT a [Files] entry: it is a symlink to rakupp.exe or nothing,
; never a copy, and Inno installs files, not links. MakePlusLink in [Code]
; makes it after the files are in place, and [UninstallDelete] takes it away.
Source: "{#Layout}\bin\rakupp.exe"; DestDir: "{app}\bin"; DestName: "raku.exe"; \
    Flags: ignoreversion; Tasks: rakualias

[UninstallDelete]
Type: files; Name: "{app}\bin\raku++.exe"

[Icons]
Name: "{group}\Raku++ (REPL)"; Filename: "{app}\bin\rakupp.exe"; WorkingDir: "{userdocs}"

[Code]
const
  UserEnvKey    = 'Environment';
  MachineEnvKey = 'SYSTEM\CurrentControlSet\Control\Session Manager\Environment';
  { Lets an ordinary user make a symlink when Developer Mode is on (Windows 10
    1703 and later); older Windows rejects the flag, hence the plain retry. }
  SymlinkAllowUnprivileged = 2;

function CreateSymbolicLink(lpSymlinkFileName, lpTargetFileName: String; dwFlags: DWORD): Boolean;
  external 'CreateSymbolicLinkW@kernel32.dll stdcall';

var
  PlusLinkMissing: Boolean;

{ raku++.exe: a symlink to rakupp.exe and nothing else -- never a copy, so
  there is one engine file and the name only points at it. The target is
  relative, resolved against the link's own folder, so a moved install keeps
  it. Windows lets an ordinary user make one only with Developer Mode on; setup
  run as administrator always can. Where neither holds there is no raku++.exe,
  and the Finished page says so. The return value of the API is a one-byte
  BOOLEAN, so FileExists is what is read rather than it. }
procedure MakePlusLink;
var
  Link: String;
begin
  Link := ExpandConstant('{app}\bin\raku++.exe');
  DeleteFile(Link);
  CreateSymbolicLink(Link, 'rakupp.exe', SymlinkAllowUnprivileged);
  if not FileExists(Link) then
    CreateSymbolicLink(Link, 'rakupp.exe', 0);
  PlusLinkMissing := not FileExists(Link);
  if PlusLinkMissing then
    Log('raku++.exe not made: Windows refused the symlink (no Developer Mode, not elevated)');
end;

{ Which PATH this install owns. A per-user install edits HKCU\Environment; an
  /ALLUSERS one edits the machine environment, which does NOT live under
  HKLM\Environment -- a detail that quietly writes to nowhere if assumed. }
function EnvRootKey: Integer;
begin
  if IsAdminInstallMode then Result := HKEY_LOCAL_MACHINE else Result := HKEY_CURRENT_USER;
end;

function EnvSubKey: String;
begin
  if IsAdminInstallMode then Result := MachineEnvKey else Result := UserEnvKey;
end;

{ RegQueryStringValue hands back REG_EXPAND_SZ data RAW, which is the whole
  point: read a PATH through anything that expands %VAR% and writing it back
  bakes today's expansion into the machine for good. }
function ReadPath(var Value: String): Boolean;
begin
  Result := RegQueryStringValue(EnvRootKey, EnvSubKey, 'Path', Value);
  if not Result then Value := '';
end;

function SameDir(const A, B: String): Boolean;
begin
  Result := Uppercase(RemoveBackslash(Trim(A))) = Uppercase(RemoveBackslash(Trim(B)));
end;

function IsOurs(const Item, Dir: String): Boolean;
begin
  Result := (Trim(Item) <> '') and SameDir(Item, Dir);
end;

function PathHasDir(const Dir: String): Boolean;
var
  Rest, Item: String;
  P: Integer;
  Done: Boolean;
begin
  Result := False;
  if not ReadPath(Rest) then Exit;
  Done := False;
  { Split so that the value's own shape survives: "a;" is two elements, the
    second empty, and "a;b" is two non-empty ones. A sentinel would invent a
    trailing element that was never there. }
  while not Done do begin
    P := Pos(';', Rest);
    if P > 0 then begin
      Item := Copy(Rest, 1, P - 1);
      Rest := Copy(Rest, P + 1, Length(Rest));
    end else begin
      Item := Rest;
      Rest := '';
      Done := True;
    end;
    if IsOurs(Item, Dir) then begin
      Result := True;
      Exit;
    end;
  end;
end;

procedure AddToPath(const Dir: String);
var
  Cur, NewValue: String;
begin
  if PathHasDir(Dir) then Exit;
  ReadPath(Cur);
  if Trim(Cur) = '' then
    NewValue := Dir
  else begin
    { drop trailing separators first: an empty PATH element is searched as the
      current directory, which is not something an installer should introduce }
    while (Length(Cur) > 0) and (Cur[Length(Cur)] = ';') do
      Cur := Copy(Cur, 1, Length(Cur) - 1);
    NewValue := Cur + ';' + Dir;
  end;
  { Written as REG_EXPAND_SZ, the type Windows itself uses here. A plain REG_SZ
    value carrying a literal %something% would change meaning -- no such PATH
    has ever been seen, and the alternative is expanding one. }
  RegWriteExpandStringValue(EnvRootKey, EnvSubKey, 'Path', NewValue);
end;

{ Takes out this install's entry and nothing else: every other element, empty
  ones included, goes back exactly as it was found. }
procedure RemoveFromPath(const Dir: String);
var
  Rest, NewValue, Item: String;
  P: Integer;
  Done, First: Boolean;
begin
  if not ReadPath(Rest) then Exit;
  if not PathHasDir(Dir) then Exit;
  NewValue := '';
  First    := True;
  Done     := False;
  while not Done do begin
    P := Pos(';', Rest);
    if P > 0 then begin
      Item := Copy(Rest, 1, P - 1);
      Rest := Copy(Rest, P + 1, Length(Rest));
    end else begin
      Item := Rest;
      Rest := '';
      Done := True;
    end;
    if not IsOurs(Item, Dir) then begin
      if not First then NewValue := NewValue + ';';
      NewValue := NewValue + Item;
      First    := False;
    end;
  end;
  RegWriteExpandStringValue(EnvRootKey, EnvSubKey, 'Path', NewValue);
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssPostInstall then
    MakePlusLink;
  if (CurStep = ssPostInstall) and WizardIsTaskSelected('addtopath') then
    AddToPath(ExpandConstant('{app}\bin'));
end;

procedure CurPageChanged(CurPageID: Integer);
begin
  if (CurPageID = wpFinished) and PlusLinkMissing then
    WizardForm.FinishedLabel.Caption := WizardForm.FinishedLabel.Caption + #13#10#13#10 +
      'raku++ was not installed: Windows makes its symlink only with Developer ' +
      'Mode on, or when setup runs as administrator. Turn one on and run setup ' +
      'again to get it.';
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usPostUninstall then
    RemoveFromPath(ExpandConstant('{app}\bin'));
end;
