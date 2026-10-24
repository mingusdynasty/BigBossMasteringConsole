; Windows installer for the Big Boss Mastering Console (Inno Setup 6, https://jrsoftware.org/isinfo.php).
;
; Build the plug-in first (Release), then compile this script from the repository root:
;   "C:\Program Files (x86)\Inno Setup 6\ISCC.exe" installer\BigBoss.iss
; or with another build folder:
;   ISCC.exe /DBuildDir=..\build-win installer\BigBoss.iss
; The installer lands in installer\Output. It asks for administrator rights once, so the VST3 goes
; where every host looks (C:\Program Files\Common Files\VST3) without copying it by hand. The CLAP
; is included when it was built (-DBB_BUILD_CLAP=ON), and the Standalone app is an option.

#define AppName "Big Boss Mastering Console"
#define AppVersion "0.1.0"
#ifndef BuildDir
  #define BuildDir "..\build"
#endif
#define Artefacts BuildDir + "\BigBoss_artefacts\Release"

[Setup]
AppId={{6B1E2E4A-7A61-4E0B-9C59-B16B055B0551}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher=mingusdynasty
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
DisableDirPage=yes
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputBaseFilename=BigBossMasteringConsole-{#AppVersion}-setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
UninstallDisplayName={#AppName}
CloseApplications=yes

[Types]
Name: "full"; Description: "VST3, CLAP and the Standalone app"
Name: "plugin"; Description: "Plug-ins only"
Name: "custom"; Description: "Choose"; Flags: iscustom

[Components]
Name: "vst3"; Description: "VST3 plug-in (Common Files\VST3)"; Types: full plugin custom; Flags: fixed
#if DirExists(Artefacts + "\CLAP")
Name: "clap"; Description: "CLAP plug-in (Common Files\CLAP)"; Types: full plugin custom
#endif
Name: "standalone"; Description: "Standalone app"; Types: full custom

[Files]
; the VST3 is a folder (a bundle): copy it whole
Source: "{#Artefacts}\VST3\{#AppName}.vst3\*"; DestDir: "{commoncf64}\VST3\{#AppName}.vst3"; Components: vst3; Flags: recursesubdirs createallsubdirs ignoreversion
#if DirExists(Artefacts + "\CLAP")
Source: "{#Artefacts}\CLAP\{#AppName}.clap"; DestDir: "{commoncf64}\CLAP"; Components: clap; Flags: ignoreversion
#endif
Source: "{#Artefacts}\Standalone\{#AppName}.exe"; DestDir: "{app}"; Components: standalone; Flags: ignoreversion
Source: "..\README.md"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\{#AppName}.exe"; Components: standalone

[UninstallDelete]
; the plug-in folder only: user presets in %APPDATA% stay
Type: filesandordirs; Name: "{commoncf64}\VST3\{#AppName}.vst3"

[Messages]
FinishedLabel=Setup has installed [name]. Rescan plug-ins in your host (in Reaper: Options > Preferences > Plug-ins > VST > Re-scan) and it appears as "{#AppName}". Quit the host before installing an update.
