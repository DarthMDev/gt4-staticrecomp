; GT4Recomp per-user installer (no administrator rights).
; Build (from the repository root, after the launcher and the game are built):
;   makensis /DVERSION=1.0.0 /DLAUNCHER_EXE=<path>\GT4Recomp.exe /DGAME_DIR=<path>\game ^
;            /DSDL3_DLL=<path>\SDL3.dll /DWEBVIEW2_BOOTSTRAPPER=<path>\MicrosoftEdgeWebview2Setup.exe ^
;            /DOUTFILE=<path>\GT4Recomp-Setup-1.0.0.exe installer\gt4recomp.nsi
; The default output name GT4Recomp-Setup-<VERSION>.exe is the asset name the launcher's updater looks for
; (launcher\crates\gt4l-core\src\releases.rs, docs/RELEASES.md).
; WEBVIEW2_BOOTSTRAPPER is Microsoft's Evergreen bootstrapper (https://go.microsoft.com/fwlink/p/?LinkId=2124703),
; Authenticode-signed by Microsoft Corporation. It is not in git; the release build records its SHA-256. The launcher
; window is a WebView2 page: the bootstrapper runs only when the runtime is missing (see WebView2Present below).
; SDL3_DLL is SDL3 3.4.18 (runtime\SDL3.dll of SDL3-3.4.18-win32-x64.zip). It ships beside GT4Recomp.exe: the
; launcher reads pads through SDL3 (launcher\crates\gt4l-platform\src\sdl_pads.rs).
; GAME_DIR must hold gt4vulkan.exe, the runtime DLLs, shaders\ and the licence texts. It is not in git.
; The player's saves and replays live in Documents\Gran Turismo 4 (found by the launcher through the Windows
; known-folder API) and are never touched by this script. Settings, the disc copy and other data live in
; %LOCALAPPDATA%\GT4Recomp and are kept by the uninstaller unless the user ticks the box on the uninstall page.

!ifndef VERSION
  !define VERSION "1.0.0"
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

Unicode true
Name "${APP_NAME}"
OutFile "${OUTFILE}"
InstallDir "$LOCALAPPDATA\Programs\${INTERNAL_NAME}"
InstallDirRegKey HKCU "${APP_KEY}" "InstallDir"
RequestExecutionLevel user
SetCompressor /SOLID lzma
ShowInstDetails show
BrandingText "${APP_NAME} ${VERSION}"

!include "MUI2.nsh"
!include "nsDialogs.nsh"
!include "LogicLib.nsh"
!include "FileFunc.nsh"

!define MUI_ICON "${__FILEDIR__}\..\launcher\assets\brand\launcher.ico"
!define MUI_UNICON "${__FILEDIR__}\..\launcher\assets\brand\launcher.ico"
!define MUI_ABORTWARNING

; Welcome: the trademark notice is the first thing the user reads.
!define MUI_WELCOMEPAGE_TEXT "GT4Recomp is an unofficial fan project, not affiliated with Sony Interactive Entertainment or Polyphony Digital.$\r$\n$\r$\nGran Turismo and the Gran Turismo 4 logo, emblem and cover are trademarks or copyrighted works of their owners. You need your own copy of Gran Turismo 4 (USA, v2.00) to play. See NOTICE.md."
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "${__FILEDIR__}\..\LICENSE"
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!define MUI_FINISHPAGE_RUN "$INSTDIR\GT4Recomp.exe"
!define MUI_FINISHPAGE_RUN_TEXT "Start ${APP_NAME}"
!insertmacro MUI_PAGE_FINISH

Var DeleteDataCheckbox
Var DeleteDataState
Var WebView2Version

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
; before the files are replaced.
Function .onInit
  Sleep 1500
FunctionEnd

!insertmacro MUI_UNPAGE_WELCOME
UninstPage custom un.DataPageCreate un.DataPageLeave
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "English"

Section "Install" SecInstall
  SetShellVarContext current

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

  ; version.json (docs/RELEASES.md): the installed GT4Recomp version and the settings schema. The launcher records the
  ; gt4-gs version in it after its first renderer install, so a fresh install has no renderer yet (null).
  ; An upgrade rewrites this program file; the installer never touches the data folder.
  FileOpen $0 "$INSTDIR\version.json" w
  FileWrite $0 '{"schema": 1, "gt4recomp": "${VERSION}", "gt4gs": null, "settings_schema": 1}$\r$\n'
  FileClose $0

  CreateDirectory "$SMPROGRAMS\${APP_NAME}"
  CreateShortCut "$SMPROGRAMS\${APP_NAME}\${APP_NAME}.lnk" "$INSTDIR\GT4Recomp.exe" "" "$INSTDIR\GT4Recomp.exe" 0
  CreateShortCut "$SMPROGRAMS\${APP_NAME}\Uninstall ${APP_NAME}.lnk" "$INSTDIR\uninstall.exe"

  WriteRegStr HKCU "${APP_KEY}" "InstallDir" "$INSTDIR"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "DisplayName" "${APP_NAME}"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "DisplayVersion" "${VERSION}"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "Publisher" "GT4Recomp contributors"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "DisplayIcon" "$INSTDIR\GT4Recomp.exe"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "UninstallString" "$\"$INSTDIR\uninstall.exe$\""
  WriteRegStr HKCU "${UNINSTALL_KEY}" "QuietUninstallString" "$\"$INSTDIR\uninstall.exe$\" /S"
  WriteRegDWORD HKCU "${UNINSTALL_KEY}" "NoModify" 1
  WriteRegDWORD HKCU "${UNINSTALL_KEY}" "NoRepair" 1
  ${GetSize} "$INSTDIR" "/S=0K" $0 $1 $2
  IntFmt $0 "0x%08X" $0
  WriteRegDWORD HKCU "${UNINSTALL_KEY}" "EstimatedSize" $0
SectionEnd

Section "Uninstall"
  SetShellVarContext current
  Delete "$SMPROGRAMS\${APP_NAME}\${APP_NAME}.lnk"
  Delete "$SMPROGRAMS\${APP_NAME}\Uninstall ${APP_NAME}.lnk"
  RMDir "$SMPROGRAMS\${APP_NAME}"

  RMDir /r "$INSTDIR\game"
  Delete "$INSTDIR\GT4Recomp.exe"
  Delete "$INSTDIR\SDL3.dll"
  Delete "$INSTDIR\NOTICE.md"
  Delete "$INSTDIR\LICENSE.txt"
  Delete "$INSTDIR\version.json"
  Delete "$INSTDIR\uninstall.exe"
  RMDir "$INSTDIR"

  DeleteRegKey HKCU "${UNINSTALL_KEY}"
  DeleteRegKey HKCU "${APP_KEY}"

  ; Saves and replays in Documents\Gran Turismo 4 are never touched here. AppData data (settings, prepared game
  ; files, the copy of the disc image in disc\, logs) stays unless the user ticked the "also delete" box, which is
  ; unchecked by default. Even then the launcher's fallback player folder (GT4Recomp\Gran Turismo 4, used only when
  ; Documents was not available) is kept. Install and update never write to either place.
  ${If} $DeleteDataState == ${BST_CHECKED}
    StrCpy $R0 "$LOCALAPPDATA\GT4Recomp"
    FindFirst $R1 $R2 "$R0\*"
    ${DoWhile} $R2 != ""
      ${If} $R2 != "."
      ${AndIf} $R2 != ".."
      ${AndIf} $R2 != "Gran Turismo 4"
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
  nsDialogs::Create 1018
  Pop $0
  ${If} $0 == error
    Abort
  ${EndIf}
  ${NSD_CreateLabel} 0 0 100% 36u "Your saves and replays in Documents\Gran Turismo 4 are always kept.$\r$\nSettings, the disc copy and other data are in $LOCALAPPDATA\GT4Recomp"
  Pop $0
  ${NSD_CreateCheckbox} 0 46u 100% 12u "Also delete settings, the disc copy and other data in AppData"
  Pop $DeleteDataCheckbox
  ${NSD_SetState} $DeleteDataCheckbox ${BST_UNCHECKED}
  nsDialogs::Show
FunctionEnd

Function un.DataPageLeave
  ${NSD_GetState} $DeleteDataCheckbox $DeleteDataState
FunctionEnd
