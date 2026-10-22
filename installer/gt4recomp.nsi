; GT4Recomp per-machine installer (1.2.1 and later; TASK-091, owner 2026-10-22). Needs administrator rights.
; Build (from the repository root, after the launcher and the game are built; every path ABSOLUTE, the .nsi too):
;   makensis /DVERSION=1.2.1 /DLAUNCHER_EXE=<path>\GT4Recomp.exe /DGAME_DIR=<path>\game ^
;            /DSDL3_DLL=<path>\SDL3.dll /DWEBVIEW2_BOOTSTRAPPER=<path>\MicrosoftEdgeWebview2Setup.exe ^
;            /DOUTFILE=<path>\GT4Recomp-Setup-1.2.1.exe <path>\installer\gt4recomp.nsi
; The default output name GT4Recomp-Setup-<VERSION>.exe is the asset name the launcher's updater looks for
; (launcher\crates\gt4l-core\src\releases.rs, docs/RELEASES.md).
; WEBVIEW2_BOOTSTRAPPER is Microsoft's Evergreen bootstrapper (https://go.microsoft.com/fwlink/p/?LinkId=2124703),
; Authenticode-signed by Microsoft Corporation. It is not in git; the release build records its SHA-256. The launcher
; window is a WebView2 page: the bootstrapper runs only when the runtime is missing (see WebView2Present below).
; SDL3_DLL is SDL3 3.4.18 (runtime\SDL3.dll of SDL3-3.4.18-win32-x64.zip). It ships beside GT4Recomp.exe: the
; launcher reads pads through SDL3 (launcher\crates\gt4l-platform\src\sdl_pads.rs).
; GAME_DIR must hold gt4vulkan.exe, the runtime DLLs, shaders\ and the licence texts. It is not in git.
;
; Layout (TASK-091; docs/BUILD.md "Slice 250", docs/release-notes/1.2.1.md):
; - Program files in $INSTDIR (default C:\Program Files\GT4Recomp; the directory page lets the player pick any folder).
; - Large data in $INSTDIR\data (disc copy, prepared files, gt4-gs, caches, sessions, logs, Textures). The launcher
;   finds it relative to GT4Recomp.exe, so the folder can be copied elsewhere (portable). This script creates it and
;   gives the local Users group (SID S-1-5-32-545) Modify on that folder only; it never writes into it.
; - User data per Windows user in %APPDATA%\GT4Recomp (settings, controls, saves, replays), written by the launcher.
; The launcher, not this script, moves the files of 1.2.0 (%LOCALAPPDATA%\GT4Recomp, Documents\Gran Turismo 4) at its
; first start, running as the player. This script only removes the old per-user program install (never its data).
; A reinstall into another folder (TASK-091, slice 252) removes the previous program folder's files and records its
; data folder as PreviousDataDir (HKLM); the launcher moves that data at its first start. A self-update runs silently
; with /S /UPDATE /D=<install folder> and starts the launcher again as the player.

!ifndef VERSION
  !define VERSION "1.2.1"
!endif
!ifndef LAUNCHER_EXE
  !error "Define LAUNCHER_EXE=<path to GT4Recomp.exe>"
!endif
!ifndef GAME_DIR
  !error "Define GAME_DIR=<folder with gt4vulkan.exe and its runtime files>"
!endif
!ifndef SDL3_DLL
  !error "Define SDL3_DLL=<path to SDL3.dll, SDL 3.4.18>"
!endif
!ifndef WEBVIEW2_BOOTSTRAPPER
  !error "Define WEBVIEW2_BOOTSTRAPPER=<path to MicrosoftEdgeWebview2Setup.exe>"
!endif
!ifndef OUTFILE
  !define OUTFILE "GT4Recomp-Setup-${VERSION}.exe"
!endif
; The Evergreen WebView2 Runtime's client id (Microsoft, "Distribute your app and the WebView2 Runtime").
!define WEBVIEW2_CLIENT "{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}"

!define APP_NAME "Gran Turismo 4"
!define INTERNAL_NAME "GT4Recomp"
!define UNINSTALL_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\${INTERNAL_NAME}"
!define APP_KEY "Software\${INTERNAL_NAME}"
; Well-known SID of the local Users group (BUILTIN\Users). Never a localized group name.
!define USERS_SID "S-1-5-32-545"
; The expected size of the data the launcher puts in $INSTDIR\data (disc copy, prepared files, gt4-gs), in KiB
; (about 5.2 GB), added to the program size in the uninstall entry.
!define EXPECTED_DATA_KB 5452595

Unicode true
Name "${APP_NAME}"
OutFile "${OUTFILE}"
InstallDir "$PROGRAMFILES64\${INTERNAL_NAME}"
RequestExecutionLevel admin
SetCompressor /SOLID lzma
ShowInstDetails show
BrandingText "${APP_NAME} ${VERSION}"

!include "MUI2.nsh"
!include "nsDialogs.nsh"
!include "LogicLib.nsh"
!include "FileFunc.nsh"
!include "x64.nsh"

!define MUI_ICON "${__FILEDIR__}\..\launcher\assets\brand\launcher.ico"
!define MUI_UNICON "${__FILEDIR__}\..\launcher\assets\brand\launcher.ico"
!define MUI_ABORTWARNING

; Welcome: the trademark notice is the first thing the user reads.
!define MUI_WELCOMEPAGE_TEXT "GT4Recomp is an unofficial fan project, not affiliated with Sony Interactive Entertainment or Polyphony Digital.$\r$\n$\r$\nGran Turismo and the Gran Turismo 4 logo, emblem and cover are trademarks or copyrighted works of their owners. You need your own copy of Gran Turismo 4 (USA, v2.00) to play. See NOTICE.md."
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "${__FILEDIR__}\..\LICENSE"
!define MUI_DIRECTORYPAGE_TEXT_TOP "GT4Recomp keeps its program files and its large data (a copy of your disc image, about 5 GB) in this folder, in its data subfolder. Your settings, saves and replays are kept for each Windows user in AppData.$\r$\n$\r$\nThe whole folder can later be copied to another drive or PC and started from there."
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
; The setup runs elevated. The launcher must start as the player (its user data and the move of the 1.2.0 files
; belong to that account), so the finish page asks Explorer to start it with the player's own rights.
!define MUI_FINISHPAGE_RUN
!define MUI_FINISHPAGE_RUN_TEXT "Start ${APP_NAME}"
!define MUI_FINISHPAGE_RUN_FUNCTION StartLauncherAsPlayer
!insertmacro MUI_PAGE_FINISH

Var DeleteDataCheckbox
Var DeleteDataState
Var WebView2Version
; 1 when /UPDATE was given: the launcher's self-update (TASK-091). Silent with /S, it starts the launcher when done.
Var UpdateRun

; Sets $WebView2Version to the installed Evergreen WebView2 Runtime version, or "" when it is missing. Microsoft's rule:
; the runtime is present when the "pv" value of its client key exists and is neither empty nor 0.0.0.0. Per-machine
; installs write it under HKLM (the WOW6432Node view on 64-bit Windows), per-user installs under HKCU.
Function WebView2Present
  StrCpy $WebView2Version ""
  SetRegView 64
  ReadRegStr $0 HKLM "SOFTWARE\WOW6432Node\Microsoft\EdgeUpdate\Clients\${WEBVIEW2_CLIENT}" "pv"
  ${If} $0 == ""
  ${OrIf} $0 == "0.0.0.0"
    ReadRegStr $0 HKLM "SOFTWARE\Microsoft\EdgeUpdate\Clients\${WEBVIEW2_CLIENT}" "pv"
  ${EndIf}
  ${If} $0 == ""
  ${OrIf} $0 == "0.0.0.0"
    ReadRegStr $0 HKCU "Software\Microsoft\EdgeUpdate\Clients\${WEBVIEW2_CLIENT}" "pv"
  ${EndIf}
  SetRegView 32
  ${If} $0 == ""
  ${OrIf} $0 == "0.0.0.0"
    ReadRegStr $0 HKLM "SOFTWARE\Microsoft\EdgeUpdate\Clients\${WEBVIEW2_CLIENT}" "pv"
  ${EndIf}
  SetRegView lastused
  ${If} $0 != ""
  ${AndIf} $0 != "0.0.0.0"
    StrCpy $WebView2Version $0
  ${EndIf}
FunctionEnd

; The launcher starts this setup from an update and exits at once. A short pause lets it release GT4Recomp.exe
; before the files are replaced. An earlier per-machine install is upgraded in its own folder (unless /D= was given).
; The launcher's self-update passes "/S /UPDATE /D=<install folder>" (TASK-091, updates.rs setup_arguments): silent,
; into the folder of the running launcher, which is then started again as the player (see the Install section).
Function .onInit
  Sleep 1500
  ${IfNot} ${RunningX64}
    MessageBox MB_OK|MB_ICONSTOP "GT4Recomp needs 64-bit Windows." /SD IDOK
    Abort
  ${EndIf}
  SetRegView 64
  ReadRegStr $0 HKLM "${APP_KEY}" "InstallDir"
  ${If} $0 != ""
  ${AndIf} $INSTDIR == "$PROGRAMFILES64\${INTERNAL_NAME}"
    StrCpy $INSTDIR $0
  ${EndIf}
  StrCpy $UpdateRun 0
  ClearErrors
  ${GetParameters} $R0
  ${GetOptions} $R0 "/UPDATE" $R1
  ${IfNot} ${Errors}
    StrCpy $UpdateRun 1
  ${EndIf}
FunctionEnd

; Explorer runs the launcher with the rights of the signed-in player, not this elevated setup's.
Function StartLauncherAsPlayer
  Exec '"$WINDIR\explorer.exe" "$INSTDIR\GT4Recomp.exe"'
FunctionEnd

; Removes one folder of the old per-user install (1.0.0 to 1.2.0) given on the stack: only the files that installer
; wrote, and only when the folder looks like it (GT4Recomp.exe or uninstall.exe there), is not the folder being
; installed now and holds no data folder. The launcher of 1.2.0 may still be exiting, so its file is retried briefly.
Function RemoveOldFolder
  Exch $R1
  Push $R2
  Push $R3
  ; C:\X\ and C:\X are the same folder: one trailing backslash is dropped before the comparison with the install
  ; folder, so the install being made is never deleted (a root such as C:\ keeps its backslash).
  StrLen $R3 $R1
  ${If} $R3 > 3
    StrCpy $R2 $R1 1 -1
    ${If} $R2 == "\"
      StrCpy $R1 $R1 -1
    ${EndIf}
  ${EndIf}
  ${If} $R1 != ""
  ${AndIf} $R1 != $INSTDIR
  ${AndIfNot} ${FileExists} "$R1\data\*.*"
    ${If} ${FileExists} "$R1\GT4Recomp.exe"
    ${OrIf} ${FileExists} "$R1\uninstall.exe"
      DetailPrint "Removing the old per-user program files in $R1"
      StrCpy $R2 0
      ${Do}
        ClearErrors
        Delete "$R1\GT4Recomp.exe"
        ${IfNot} ${Errors}
          ${Break}
        ${EndIf}
        IntOp $R2 $R2 + 1
        ${If} $R2 >= 10
          DetailPrint "$R1\GT4Recomp.exe is in use; the launcher removes it at its next start"
          ${Break}
        ${EndIf}
        Sleep 500
      ${Loop}
      Delete "$R1\SDL3.dll"
      Delete "$R1\NOTICE.md"
      Delete "$R1\LICENSE.txt"
      Delete "$R1\version.json"
      Delete "$R1\uninstall.exe"
      RMDir /r "$R1\game"
      RMDir "$R1"
    ${EndIf}
  ${EndIf}
  Pop $R3
  Pop $R2
  Pop $R1
FunctionEnd

; The per-user install of 1.0.0 to 1.2.0, as the account running this setup sees it: its program folder (registry
; InstallDir, the uninstall entry's InstallLocation, and the default %LOCALAPPDATA%\Programs\GT4Recomp), its Start-menu
; shortcuts and its HKCU entries. %LOCALAPPDATA%\GT4Recomp (the 1.2.0 data) is never touched here. When an administrator
; other than the player runs this setup, the launcher cleans the player's own leftovers at its first start.
Function RemoveOldPerUserInstall
  SetShellVarContext current
  ReadRegStr $0 HKCU "${APP_KEY}" "InstallDir"
  Push $0
  Call RemoveOldFolder
  ReadRegStr $0 HKCU "${UNINSTALL_KEY}" "InstallLocation"
  Push $0
  Call RemoveOldFolder
  Push "$LOCALAPPDATA\Programs\${INTERNAL_NAME}"
  Call RemoveOldFolder
  Delete "$SMPROGRAMS\${APP_NAME}\${APP_NAME}.lnk"
  Delete "$SMPROGRAMS\${APP_NAME}\Uninstall ${APP_NAME}.lnk"
  RMDir "$SMPROGRAMS\${APP_NAME}"
  DeleteRegKey HKCU "${UNINSTALL_KEY}"
  DeleteRegKey HKCU "${APP_KEY}"
FunctionEnd

; Normalises the folder in $0 for comparisons: one trailing backslash is dropped (a root such as C:\ keeps it). The
; comparisons of NSIS strings (StrCmp, LogicLib ==) ignore case.
Function NormalizeFolder
  StrLen $1 $0
  ${If} $1 > 3
    StrCpy $2 $0 1 -1
    ${If} $2 == "\"
      StrCpy $0 $0 -1
    ${EndIf}
  ${EndIf}
FunctionEnd

; A reinstall into another folder (TASK-091): the program folder of the previous per-machine install (HKLM InstallDir,
; 64-bit view) is removed, its program files only, then the folder itself when it is empty. Its data folder is never
; touched: when it exists, PreviousDataDir (HKLM) names it, and the launcher moves its large data into this install's
; data folder at its first start. Nothing happens when the previous folder is this one (an update) or does not hold
; GT4Recomp.exe.
Function RemovePreviousProgramFolder
  SetRegView 64
  ReadRegStr $R5 HKLM "${APP_KEY}" "InstallDir"
  ${If} $R5 == ""
    Return
  ${EndIf}
  StrCpy $0 $R5
  Call NormalizeFolder
  StrCpy $R5 $0
  StrCpy $0 $INSTDIR
  Call NormalizeFolder
  ${If} $R5 == $0
    Return
  ${EndIf}
  ${IfNot} ${FileExists} "$R5\GT4Recomp.exe"
    Return
  ${EndIf}
  DetailPrint "The previous install in $R5 is replaced by $INSTDIR"
  ${If} ${FileExists} "$R5\data\*.*"
    WriteRegStr HKLM "${APP_KEY}" "PreviousDataDir" "$R5\data"
    DetailPrint "Its data folder $R5\data is kept; the launcher moves it into $INSTDIR\data at its first start"
  ${EndIf}
  ; The launcher of the previous install may still be closing: its file is retried briefly.
  StrCpy $R6 0
  ${Do}
    ClearErrors
    Delete "$R5\GT4Recomp.exe"
    ${IfNot} ${Errors}
      ${Break}
    ${EndIf}
    IntOp $R6 $R6 + 1
    ${If} $R6 >= 10
      DetailPrint "$R5\GT4Recomp.exe is in use; it stays there"
      ${Break}
    ${EndIf}
    Sleep 500
  ${Loop}
  Delete "$R5\SDL3.dll"
  Delete "$R5\NOTICE.md"
  Delete "$R5\LICENSE.txt"
  Delete "$R5\version.json"
  Delete "$R5\uninstall.exe"
  RMDir /r "$R5\game"
  ; Not recursive: the folder goes only when it is empty (a data folder that stays keeps it).
  RMDir "$R5"
FunctionEnd

!insertmacro MUI_UNPAGE_WELCOME
UninstPage custom un.DataPageCreate un.DataPageLeave
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "English"

Section "Install" SecInstall
  SetRegView 64
  Call RemoveOldPerUserInstall
  ; A reinstall into another folder: the previous program folder goes, its data is recorded for the launcher (TASK-091).
  Call RemovePreviousProgramFolder
  SetShellVarContext all

  ; The launcher needs the Microsoft Edge WebView2 Runtime (built into Windows 11, usually present on Windows 10).
  ; When it is missing, Microsoft's Evergreen bootstrapper (embedded) downloads and installs it.
  Call WebView2Present
  ${If} $WebView2Version == ""
    DetailPrint "Installing the Microsoft Edge WebView2 Runtime (needed by the GT4Recomp window)..."
    InitPluginsDir
    File "/oname=$PLUGINSDIR\MicrosoftEdgeWebview2Setup.exe" "${WEBVIEW2_BOOTSTRAPPER}"
    ExecWait '"$PLUGINSDIR\MicrosoftEdgeWebview2Setup.exe" /silent /install' $1
    DetailPrint "WebView2 bootstrapper exit code: $1"
    Call WebView2Present
    ${If} $WebView2Version == ""
      MessageBox MB_OK|MB_ICONEXCLAMATION "The Microsoft Edge WebView2 Runtime could not be installed (it needs an internet connection).$\r$\n$\r$\nGT4Recomp will be installed, but its window needs WebView2. Install it from https://go.microsoft.com/fwlink/p/?LinkId=2124703 and then start GT4Recomp." /SD IDOK
    ${Else}
      DetailPrint "WebView2 Runtime $WebView2Version installed."
    ${EndIf}
  ${Else}
    DetailPrint "WebView2 Runtime $WebView2Version found."
  ${EndIf}

  SetOutPath "$INSTDIR"
  File "/oname=GT4Recomp.exe" "${LAUNCHER_EXE}"
  File "/oname=SDL3.dll" "${SDL3_DLL}"
  File "/oname=NOTICE.md" "${__FILEDIR__}\..\NOTICE.md"
  File "/oname=LICENSE.txt" "${__FILEDIR__}\..\LICENSE"

  SetOutPath "$INSTDIR\game"
  File /r "${GAME_DIR}\*.*"

  SetOutPath "$INSTDIR"
  WriteUninstaller "$INSTDIR\uninstall.exe"

  ; version.json (docs/RELEASES.md): the installed GT4Recomp version and the settings schema. The launcher cannot write
  ; this program folder, so it keeps its own copy (with the gt4-gs version) in the data folder.
  FileOpen $0 "$INSTDIR\version.json" w
  FileWrite $0 '{"schema": 1, "gt4recomp": "${VERSION}", "gt4gs": null, "settings_schema": 1}$\r$\n'
  FileClose $0

  ; The large data folder. Created when missing; an upgrade never writes into it. The local Users group gets Modify on
  ; this folder (inherited by its files) so the launcher, running as any player, can write it; the rest of the program
  ; folder stays protected.
  ${IfNot} ${FileExists} "$INSTDIR\data\*.*"
    CreateDirectory "$INSTDIR\data"
  ${EndIf}
  nsExec::ExecToLog '"$SYSDIR\icacls.exe" "$INSTDIR\data" /grant *${USERS_SID}:(OI)(CI)M'
  Pop $0
  ${If} $0 != 0
    DetailPrint "icacls exit code $0: the data folder may not be writable for players"
    MessageBox MB_OK|MB_ICONEXCLAMATION "The permissions of $INSTDIR\data could not be set (icacls exit code $0). GT4Recomp will keep its data in your AppData folder instead." /SD IDOK
  ${EndIf}

  CreateDirectory "$SMPROGRAMS\${APP_NAME}"
  CreateShortCut "$SMPROGRAMS\${APP_NAME}\${APP_NAME}.lnk" "$INSTDIR\GT4Recomp.exe" "" "$INSTDIR\GT4Recomp.exe" 0
  CreateShortCut "$SMPROGRAMS\${APP_NAME}\Uninstall ${APP_NAME}.lnk" "$INSTDIR\uninstall.exe"

  WriteRegStr HKLM "${APP_KEY}" "InstallDir" "$INSTDIR"
  WriteRegStr HKLM "${UNINSTALL_KEY}" "DisplayName" "${APP_NAME}"
  WriteRegStr HKLM "${UNINSTALL_KEY}" "DisplayVersion" "${VERSION}"
  WriteRegStr HKLM "${UNINSTALL_KEY}" "Publisher" "GT4Recomp contributors"
  WriteRegStr HKLM "${UNINSTALL_KEY}" "DisplayIcon" "$INSTDIR\GT4Recomp.exe"
  WriteRegStr HKLM "${UNINSTALL_KEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKLM "${UNINSTALL_KEY}" "UninstallString" "$\"$INSTDIR\uninstall.exe$\""
  WriteRegStr HKLM "${UNINSTALL_KEY}" "QuietUninstallString" "$\"$INSTDIR\uninstall.exe$\" /S"
  WriteRegDWORD HKLM "${UNINSTALL_KEY}" "NoModify" 1
  WriteRegDWORD HKLM "${UNINSTALL_KEY}" "NoRepair" 1
  ; EstimatedSize (KiB): the program files plus the expected data, whatever the data folder holds today.
  ${GetSize} "$INSTDIR" "/S=0K" $0 $1 $2
  ${GetSize} "$INSTDIR\data" "/S=0K" $3 $1 $2
  IntOp $0 $0 - $3
  IntOp $0 $0 + ${EXPECTED_DATA_KB}
  IntFmt $0 "0x%08X" $0
  WriteRegDWORD HKLM "${UNINSTALL_KEY}" "EstimatedSize" $0

  ; A silent self-update (/S /UPDATE, TASK-091) has no finish page: the launcher starts here instead, as the player
  ; (Explorer), so the player gets the updated program back. Without /UPDATE, or when interactive, nothing changes.
  ; Diagnostics for the self-update path (TASK-091): what this run saw, appended to data\logs\setup.log.
  StrCpy $R5 "interactive"
  ${If} ${Silent}
    StrCpy $R5 "silent"
  ${EndIf}
  CreateDirectory "$INSTDIR\data\logs"
  ClearErrors
  FileOpen $R6 "$INSTDIR\data\logs\setup.log" a
  ${IfNot} ${Errors}
    FileSeek $R6 0 END
    FileWrite $R6 "setup ${VERSION}: mode $R5, update flag $UpdateRun, command line: $CMDLINE$\r$\n"
  ${EndIf}
  ${If} $UpdateRun == 1
  ${AndIf} ${Silent}
    Call StartLauncherAsPlayer
    ${IfNot} ${Errors}
      FileWrite $R6 "setup ${VERSION}: launcher start requested through Explorer$\r$\n"
    ${EndIf}
  ${EndIf}
  FileClose $R6
SectionEnd

Section "Uninstall"
  SetRegView 64
  SetShellVarContext all
  Delete "$SMPROGRAMS\${APP_NAME}\${APP_NAME}.lnk"
  Delete "$SMPROGRAMS\${APP_NAME}\Uninstall ${APP_NAME}.lnk"
  RMDir "$SMPROGRAMS\${APP_NAME}"

  ; The program folder, its data folder included (the disc copy can be made again from the player's own image).
  RMDir /r "$INSTDIR\game"
  RMDir /r "$INSTDIR\data"
  Delete "$INSTDIR\GT4Recomp.exe"
  Delete "$INSTDIR\SDL3.dll"
  Delete "$INSTDIR\NOTICE.md"
  Delete "$INSTDIR\LICENSE.txt"
  Delete "$INSTDIR\version.json"
  Delete "$INSTDIR\uninstall.exe"
  RMDir "$INSTDIR"

  DeleteRegKey HKLM "${UNINSTALL_KEY}"
  DeleteRegKey HKLM "${APP_KEY}"

  ; Per-user data stays unless the box was ticked, and then only for the Windows account running this uninstall: its
  ; %APPDATA%\GT4Recomp (settings, controls, saves, replays) and the 1.2.0 leftovers or write fallback in
  ; %LOCALAPPDATA%\GT4Recomp. Folders named "Gran Turismo 4..." (old save libraries) are kept even then.
  ${If} $DeleteDataState == ${BST_CHECKED}
    SetShellVarContext current
    RMDir /r "$APPDATA\GT4Recomp"
    StrCpy $R0 "$LOCALAPPDATA\GT4Recomp"
    FindFirst $R1 $R2 "$R0\*"
    ${DoWhile} $R2 != ""
      StrCpy $R3 $R2 14
      ${If} $R2 != "."
      ${AndIf} $R2 != ".."
      ${AndIf} $R3 != "Gran Turismo 4"
        ${If} ${FileExists} "$R0\$R2\*.*"
          RMDir /r "$R0\$R2"
        ${Else}
          Delete "$R0\$R2"
        ${EndIf}
      ${EndIf}
      FindNext $R1 $R2
    ${Loop}
    FindClose $R1
    RMDir "$R0"
  ${EndIf}
SectionEnd

Function un.DataPageCreate
  SetShellVarContext current
  nsDialogs::Create 1018
  Pop $0
  ${If} $0 == error
    Abort
  ${EndIf}
  ${NSD_CreateLabel} 0 0 100% 52u "GT4Recomp is removed from $INSTDIR, with its data folder (the copy of your disc image, gt4-gs, logs and custom textures). Setup can copy your disc image again later.$\r$\n$\r$\nYour settings, saves and replays are in $APPDATA\GT4Recomp. They are kept unless you tick the box below."
  Pop $0
  ${NSD_CreateCheckbox} 0 60u 100% 24u "Also delete my settings, saves and replays. This acts only for the Windows user running this uninstall ($APPDATA\GT4Recomp); other users keep theirs."
  Pop $DeleteDataCheckbox
  ${NSD_SetState} $DeleteDataCheckbox ${BST_UNCHECKED}
  nsDialogs::Show
FunctionEnd

Function un.DataPageLeave
  ${NSD_GetState} $DeleteDataCheckbox $DeleteDataState
FunctionEnd
