; SPDX-License-Identifier: GPL-3.0-or-later
; Generated payload macros enumerate only the package manifest's app files.
Unicode true
RequestExecutionLevel user
ManifestDPIAware true
ManifestSupportedOS all
SetCompressor /SOLID lzma
SetCompressorDictSize 32
SetDatablockOptimize on
CRCCheck on
AllowSkipFiles off
SetOverwrite on

!include "MUI2.nsh"
!include "LogicLib.nsh"
!include "FileFunc.nsh"
!include "WinVer.nsh"
!include "x64.nsh"
!include "${DESKTOP_PAYLOAD_INCLUDE}"

Name "Desktop Melee ${DESKTOP_VERSION}"
OutFile "${DESKTOP_SETUP_OUTPUT}"
InstallDir "$LOCALAPPDATA\Programs\DesktopMelee"
BrandingText "Desktop Melee"
ShowInstDetails show
ShowUninstDetails show
VIProductVersion "${DESKTOP_VERSION}.0"
VIAddVersionKey "ProductName" "Desktop Melee"
VIAddVersionKey "FileDescription" "Desktop Melee per-user setup"
VIAddVersionKey "FileVersion" "${DESKTOP_VERSION}"
VIAddVersionKey "ProductVersion" "${DESKTOP_VERSION}"
VIAddVersionKey "LegalCopyright" "Desktop Melee contributors; see bundled licenses"

Var SetupMutex
Var CheckedPath
Var FileHandle
Var AutoUpdate
Var ParentPid

!define APP_KEY "Software\DesktopMelee"
!define UNINSTALL_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\DesktopMelee"
!define MUI_ABORTWARNING
!define MUI_WELCOMEPAGE_TITLE "Install Desktop Melee ${DESKTOP_VERSION}"
!define MUI_WELCOMEPAGE_TEXT "This installs Desktop Melee for your Windows account.$\r$\n$\r$\nYou will choose your own Melee USA 1.02 disc image in the launcher. No game files are included.$\r$\n$\r$\nClose Desktop Melee and any running match before continuing. Existing saves and settings are kept."
!define MUI_FINISHPAGE_RUN "$INSTDIR\DesktopMelee.exe"
!define MUI_FINISHPAGE_RUN_TEXT "Open Desktop Melee"
!define MUI_UNFINISHPAGE_TEXT "Desktop Melee's installed app files have been removed.$\r$\n$\r$\nYour saves, settings, disc image, and files you added have been kept."
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_UNPAGE_FINISH
!insertmacro MUI_LANGUAGE "English"

; Setup and uninstall share a per-session mutex. We never terminate a match
; or other process to force an upgrade through.
!macro SetupInit Prefix
Function ${Prefix}.onInit
    SetShellVarContext current
    SetRegView 64
    ${IfNot} ${RunningX64}
        MessageBox MB_OK|MB_ICONSTOP "Desktop Melee requires 64-bit Windows 10 or later."
        Abort
    ${EndIf}
    ${IfNot} ${AtLeastWin10}
        MessageBox MB_OK|MB_ICONSTOP "Desktop Melee requires Windows 10 or later."
        Abort
    ${EndIf}
    System::Call 'kernel32::CreateMutexW(p 0, i 0, w "Local\DesktopMelee.Setup") p .r0 ?e'
    Pop $1
    StrCpy $SetupMutex $0
    ${If} $0 == 0
    ${OrIf} $1 == 183
        MessageBox MB_OK|MB_ICONEXCLAMATION "Another Desktop Melee setup or uninstall is running. Close it and try again."
        Abort
    ${EndIf}
!if "${Prefix}" == ""
    ; In-app updates launch setup just before the old launcher exits. Wait for
    ; that exact parent process before probing or replacing any app files.
    StrCpy $AutoUpdate ""
    StrCpy $ParentPid ""
    ${GetParameters} $9
    ${GetOptions} $9 "/AUTOUPDATE=" $AutoUpdate
    ${GetOptions} $9 "/PARENTPID=" $ParentPid
    ${If} $AutoUpdate == "1"
    ${AndIf} $ParentPid != ""
        System::Call 'kernel32::OpenProcess(i 0x00100000, i 0, i $ParentPid) p .r2'
        ${If} $2 != 0
            System::Call 'kernel32::WaitForSingleObject(p r2, i 30000) i .r3'
            System::Call 'kernel32::CloseHandle(p r2)'
            ${If} $3 == 258
                SetErrorLevel 3
                Abort "Desktop Melee did not close in time for the in-app update."
            ${EndIf}
        ${EndIf}
    ${EndIf}
    ; Reuse the recorded location on upgrades; no administrator path is used.
    StrCpy $INSTDIR "$LOCALAPPDATA\Programs\DesktopMelee"
    ReadRegStr $0 HKCU "${APP_KEY}" "InstallDir"
    ${If} $0 != ""
        StrCpy $INSTDIR $0
    ${EndIf}
!else
    ; A copied uninstaller must not remove similarly named files elsewhere.
    ReadRegStr $0 HKCU "${APP_KEY}" "InstallDir"
    ${If} $0 != $INSTDIR
        MessageBox MB_OK|MB_ICONSTOP "Run Uninstall from the registered Desktop Melee installation, or reinstall Desktop Melee first."
        Abort
    ${EndIf}
!endif
FunctionEnd
Function ${Prefix}.onGUIEnd
    ${If} $SetupMutex != ""
        System::Call 'kernel32::CloseHandle(p $SetupMutex)'
    ${EndIf}
FunctionEnd
!macroend
!insertmacro SetupInit ""
!insertmacro SetupInit "un"

Function .onInstSuccess
    ${If} $AutoUpdate == "1"
        Exec '"$INSTDIR\DesktopMelee.exe"'
    ${EndIf}
FunctionEnd

; OPEN_EXISTING + GENERIC_WRITE + no sharing probes image/file locks without
; truncating or writing a byte. This catches both a running launcher and an
; independently running melee.exe, including loaded runtime DLLs.
!macro WriteLockCheck Prefix
Function ${Prefix}CheckWritable
    Pop $CheckedPath
retry:
    IfFileExists "$CheckedPath" 0 done
    System::Call 'kernel32::CreateFileW(w "$CheckedPath", i 0x40000000, i 0, p 0, i 3, i 0x80, p 0) p .r0'
    StrCpy $FileHandle $0
    ${If} $FileHandle != -1
        System::Call 'kernel32::CloseHandle(p $FileHandle)'
        Goto done
    ${EndIf}
    IfSilent cancelled
    MessageBox MB_RETRYCANCEL|MB_ICONEXCLAMATION "Close Desktop Melee and any running match, then choose Retry.$\r$\n$\r$\nThis app file is in use or cannot be changed:$\r$\n$CheckedPath" IDRETRY retry
cancelled:
    SetErrorLevel 2
    Abort "Setup stopped before changing the app files. Close the running app and try again."
done:
FunctionEnd
!macroend
!insertmacro WriteLockCheck ""
!insertmacro WriteLockCheck "un."

Section "Desktop Melee (required)" AppSection
    SectionIn RO
    SetShellVarContext current
    SetRegView 64
    ; Check all existing app files before extracting the first replacement.
    !insertmacro DesktopCheckFiles ""
    Push "$INSTDIR\Uninstall.exe"
    Call CheckWritable
    ; AllowSkipFiles off also makes an extraction-time lock race Retry/Cancel,
    ; never Ignore. No reboot replacement or deferred overwrite is used.
    !insertmacro DesktopInstallFiles
    SetOutPath "$INSTDIR"
    WriteUninstaller "$INSTDIR\Uninstall.exe"
    WriteRegStr HKCU "${APP_KEY}" "InstallDir" "$INSTDIR"
    WriteRegStr HKCU "${UNINSTALL_KEY}" "DisplayName" "Desktop Melee"
    WriteRegStr HKCU "${UNINSTALL_KEY}" "DisplayVersion" "${DESKTOP_VERSION}"
    WriteRegStr HKCU "${UNINSTALL_KEY}" "Publisher" "Desktop Melee contributors"
    WriteRegStr HKCU "${UNINSTALL_KEY}" "InstallLocation" "$INSTDIR"
    WriteRegStr HKCU "${UNINSTALL_KEY}" "DisplayIcon" "$INSTDIR\DesktopMelee.exe"
    WriteRegStr HKCU "${UNINSTALL_KEY}" "UninstallString" '"$INSTDIR\Uninstall.exe"'
    WriteRegStr HKCU "${UNINSTALL_KEY}" "QuietUninstallString" '"$INSTDIR\Uninstall.exe" /S'
    WriteRegStr HKCU "${UNINSTALL_KEY}" "URLInfoAbout" "https://github.com/Bluesguitartravis-droid/desktop-melee"
    WriteRegStr HKCU "${UNINSTALL_KEY}" "URLUpdateInfo" "https://github.com/Bluesguitartravis-droid/desktop-melee/releases"
    WriteRegDWORD HKCU "${UNINSTALL_KEY}" "EstimatedSize" ${DESKTOP_SIZE_KIB}
    WriteRegDWORD HKCU "${UNINSTALL_KEY}" "NoModify" 1
    WriteRegDWORD HKCU "${UNINSTALL_KEY}" "NoRepair" 1
    CreateDirectory "$SMPROGRAMS\Desktop Melee"
    CreateShortCut "$SMPROGRAMS\Desktop Melee\Desktop Melee.lnk" "$INSTDIR\DesktopMelee.exe"
    CreateShortCut "$SMPROGRAMS\Desktop Melee\Uninstall Desktop Melee.lnk" "$INSTDIR\Uninstall.exe"
SectionEnd

Section "Desktop shortcut" DesktopShortcut
    CreateShortCut "$DESKTOP\Desktop Melee.lnk" "$INSTDIR\DesktopMelee.exe"
SectionEnd

!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
    !insertmacro MUI_DESCRIPTION_TEXT ${AppSection} "The launcher, game engine, and required app files. Your disc image and saves stay local."
    !insertmacro MUI_DESCRIPTION_TEXT ${DesktopShortcut} "Add a Desktop Melee shortcut to your desktop."
!insertmacro MUI_FUNCTION_DESCRIPTION_END

Function un.DeleteOwnedFile
    Pop $CheckedPath
retry:
    IfFileExists "$CheckedPath" 0 done
    ClearErrors
    Delete "$CheckedPath"
    IfErrors 0 done
    IfSilent cancelled
    MessageBox MB_RETRYCANCEL|MB_ICONEXCLAMATION "Close Desktop Melee and any running match, then choose Retry.$\r$\n$\r$\nThis app file could not be removed:$\r$\n$CheckedPath" IDRETRY retry
cancelled:
    SetErrorLevel 2
    Abort "Uninstall stopped. Your saves and settings have been kept."
done:
FunctionEnd

Section "Uninstall"
    SetShellVarContext current
    SetRegView 64
    !insertmacro DesktopCheckFiles "un."
    ; Static, manifest-derived file names only. Never recursively delete the
    ; install folder, user/ saves/cache, an image, or an external settings path.
    !insertmacro DesktopRemoveFiles
    Delete "$DESKTOP\Desktop Melee.lnk"
    Delete "$SMPROGRAMS\Desktop Melee\Desktop Melee.lnk"
    Delete "$SMPROGRAMS\Desktop Melee\Uninstall Desktop Melee.lnk"
    RMDir "$SMPROGRAMS\Desktop Melee"
    DeleteRegKey HKCU "${UNINSTALL_KEY}"
    DeleteRegValue HKCU "${APP_KEY}" "InstallDir"
    DeleteRegKey /ifempty HKCU "${APP_KEY}"
    Delete "$INSTDIR\Uninstall.exe"
    ; Empty-only directory removal deliberately leaves user-added files.
    RMDir "$INSTDIR"
SectionEnd
