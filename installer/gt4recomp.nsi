; GT4Recomp per-user installer (no administrator rights).
; Build (from the repository root, after the launcher and the game are built):
;   makensis /DVERSION=1.0.0 /DLAUNCHER_EXE=<path>\GT4Recomp.exe /DGAME_DIR=<path>\game ^
;            /DSDL3_DLL=<path>\SDL3.dll /DOUTFILE=<path>\GT4Recomp-1.0.0-setup.exe installer\gt4recomp.nsi
; SDL3_DLL is SDL3 3.4.18 (runtime\SDL3.dll of SDL3-3.4.18-win32-x64.zip). It ships beside GT4Recomp.exe: the
; launcher reads pads through SDL3 (launcher\crates\gt4l-platform\src\sdl_pads.rs).
; GAME_DIR must hold gt4vulkan.exe, the runtime DLLs, shaders\ and the licence texts. It is not in git.
; Saves and settings live in %LOCALAPPDATA%\GT4Recomp and are kept by the uninstaller unless the user ticks
; the box on the uninstall page.

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
!ifndef OUTFILE
  !define OUTFILE "GT4Recomp-${VERSION}-setup.exe"
!endif

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
  Delete "$INSTDIR\NOTICE.md"
  Delete "$INSTDIR\LICENSE.txt"
  Delete "$INSTDIR\version.json"
  Delete "$INSTDIR\uninstall.exe"
  RMDir "$INSTDIR"

  DeleteRegKey HKCU "${UNINSTALL_KEY}"
  DeleteRegKey HKCU "${APP_KEY}"

  ; User data (saves, replays, settings, prepared game files and the copy of the disc image in disc\) stays unless
  ; the user ticked the "also delete" box, which is unchecked by default. Install and update never write there.
  ${If} $DeleteDataState == ${BST_CHECKED}
    RMDir /r "$LOCALAPPDATA\GT4Recomp"
  ${EndIf}
SectionEnd

Function un.DataPageCreate
  nsDialogs::Create 1018
  Pop $0
  ${If} $0 == error
    Abort
  ${EndIf}
  ${NSD_CreateLabel} 0 0 100% 36u "Your saves, replays, settings and game files (including the copy of your disc image) are in:$\r$\n$LOCALAPPDATA\GT4Recomp"
  Pop $0
  ${NSD_CreateCheckbox} 0 46u 100% 12u "Also delete my saves, replays, settings and game files (the disc image copy)"
  Pop $DeleteDataCheckbox
  ${NSD_SetState} $DeleteDataCheckbox ${BST_UNCHECKED}
  nsDialogs::Show
FunctionEnd

Function un.DataPageLeave
  ${NSD_GetState} $DeleteDataCheckbox $DeleteDataState
FunctionEnd
