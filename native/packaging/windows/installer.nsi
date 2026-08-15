Unicode true
RequestExecutionLevel user

!ifndef APP_SOURCE
  !error "APP_SOURCE is required"
!endif
!ifndef VERSION
  !define VERSION "dev"
!endif
!ifndef OUTPUT_FILE
  !define OUTPUT_FILE "CyberSnapper-setup.exe"
!endif
!ifndef APP_ICON
  !error "APP_ICON is required"
!endif

Name "CyberSnapper"
OutFile "${OUTPUT_FILE}"
Icon "${APP_ICON}"
UninstallIcon "${APP_ICON}"
InstallDir "$LOCALAPPDATA\Programs\CyberSnapper"
InstallDirRegKey HKCU "Software\CyberBrand\CyberSnapper" "InstallDir"

Page directory
Page instfiles
UninstPage uninstConfirm
UninstPage instfiles

Section "CyberSnapper" SEC_MAIN
  SetOutPath "$INSTDIR"
  File /r "${APP_SOURCE}\*"
  WriteRegStr HKCU "Software\CyberBrand\CyberSnapper" "InstallDir" "$INSTDIR"
  WriteUninstaller "$INSTDIR\Uninstall.exe"

  CreateDirectory "$SMPROGRAMS\CyberSnapper"
  CreateShortcut "$SMPROGRAMS\CyberSnapper\CyberSnapper.lnk" "$INSTDIR\bin\CyberSnapper.exe"
  CreateShortcut "$DESKTOP\CyberSnapper.lnk" "$INSTDIR\bin\CyberSnapper.exe"

  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\CyberSnapper" "DisplayName" "CyberSnapper"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\CyberSnapper" "DisplayVersion" "${VERSION}"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\CyberSnapper" "DisplayIcon" "$INSTDIR\bin\CyberSnapper.exe"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\CyberSnapper" "Publisher" "CYBER BRAND"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\CyberSnapper" "URLInfoAbout" "https://cyberbrand.net"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\CyberSnapper" "UninstallString" '$\"$INSTDIR\Uninstall.exe$\"'
  WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\CyberSnapper" "NoModify" 1
  WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\CyberSnapper" "NoRepair" 1
SectionEnd

Section "Uninstall"
  Delete "$DESKTOP\CyberSnapper.lnk"
  Delete "$SMPROGRAMS\CyberSnapper\CyberSnapper.lnk"
  RMDir "$SMPROGRAMS\CyberSnapper"
  DeleteRegKey HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\CyberSnapper"
  DeleteRegKey HKCU "Software\CyberBrand\CyberSnapper"
  RMDir /r "$INSTDIR"
SectionEnd
