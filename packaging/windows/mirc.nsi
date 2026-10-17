; NSIS installer for the MIRC client
Unicode true
!include "MUI2.nsh"

!define APPNAME "MIRC"
!define VERSION "0.1.0"
!define PUBLISHER "MIRC"
!define UNINSTKEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\MIRC"

Name "${APPNAME} ${VERSION}"
OutFile "..\..\build\windows\MIRC-Setup-${VERSION}.exe"
InstallDir "$PROGRAMFILES64\MIRC"
InstallDirRegKey HKLM "Software\MIRC" "InstallDir"
RequestExecutionLevel admin
SetCompressor /SOLID lzma

!define MUI_ICON "mirc.ico"
!define MUI_UNICON "mirc.ico"
!define MUI_FINISHPAGE_RUN "$INSTDIR\bin\mirc.exe"
!define MUI_FINISHPAGE_RUN_TEXT "Launch MIRC now"

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"

Section "MIRC" SecMain
  SetOutPath "$INSTDIR"
  File /r "..\..\build\windows\MIRC\*.*"
  WriteUninstaller "$INSTDIR\Uninstall.exe"
  WriteRegStr HKLM "Software\MIRC" "InstallDir" "$INSTDIR"
  CreateDirectory "$SMPROGRAMS\MIRC"
  CreateShortcut "$SMPROGRAMS\MIRC\MIRC.lnk" "$INSTDIR\bin\mirc.exe" "" "$INSTDIR\mirc.ico"
  CreateShortcut "$SMPROGRAMS\MIRC\Uninstall MIRC.lnk" "$INSTDIR\Uninstall.exe"
  CreateShortcut "$DESKTOP\MIRC.lnk" "$INSTDIR\bin\mirc.exe" "" "$INSTDIR\mirc.ico"
  WriteRegStr HKLM "${UNINSTKEY}" "DisplayName" "${APPNAME}"
  WriteRegStr HKLM "${UNINSTKEY}" "DisplayVersion" "${VERSION}"
  WriteRegStr HKLM "${UNINSTKEY}" "Publisher" "${PUBLISHER}"
  WriteRegStr HKLM "${UNINSTKEY}" "DisplayIcon" "$INSTDIR\mirc.ico"
  WriteRegStr HKLM "${UNINSTKEY}" "UninstallString" '"$INSTDIR\Uninstall.exe"'
  WriteRegDWORD HKLM "${UNINSTKEY}" "NoModify" 1
  WriteRegDWORD HKLM "${UNINSTKEY}" "NoRepair" 1
SectionEnd

Section "Uninstall"
  RMDir /r "$INSTDIR"
  Delete "$DESKTOP\MIRC.lnk"
  RMDir /r "$SMPROGRAMS\MIRC"
  DeleteRegKey HKLM "${UNINSTKEY}"
  DeleteRegKey HKLM "Software\MIRC"
SectionEnd
