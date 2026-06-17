; Inno Setup script for OBS Live Translate.
; Compiled in CI with:  ISCC /DMyAppVersion=<version> installer\obs-live-translate.iss
; Installs the plugin DLL into the detected OBS Studio install directory.

#ifndef MyAppVersion
  #define MyAppVersion "0.0.0"
#endif
#define MyAppName "OBS Live Translate"
#define MyAppPublisher "weisunglee"
#define MyAppURL "https://github.com/weisunglee/obs-live-translate"

[Setup]
; Keep AppId stable across versions so upgrades/uninstall work.
AppId={{8F3A2C1E-5B6D-4A7E-9C2F-1D3E4F5A6B7C}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}/issues
DefaultDirName={code:GetOBSDir}
DisableProgramGroupPage=yes
PrivilegesRequired=admin
ArchitecturesAllowed=x64
ArchitecturesInstallIn64BitMode=x64
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
UninstallDisplayName={#MyAppName}
OutputBaseFilename=obs-live-translate-{#MyAppVersion}-windows-x64-installer
; Use Restart Manager to detect/close OBS if it has the DLL open.
CloseApplications=yes
RestartApplications=no

[Files]
Source: "..\build_x64\RelWithDebInfo\obs-live-translate.dll"; \
  DestDir: "{app}\obs-plugins\64bit"; Flags: ignoreversion
; If the plugin ever ships a data/ tree (locale, etc.), add it here:
; Source: "..\data\*"; DestDir: "{app}\data\obs-plugins\obs-live-translate"; \
;   Flags: recursesubdirs ignoreversion

[Code]
{ Resolve the OBS Studio install directory from the registry written by OBS's
  own installer; fall back to the default Program Files location. }
function GetOBSDir(Param: String): String;
var
  Path: String;
begin
  if RegQueryStringValue(HKLM, 'SOFTWARE\OBS Studio', '', Path) and (Path <> '') then
    Result := Path
  else
    Result := ExpandConstant('{commonpf64}\obs-studio');
end;
