; Hooks of the Windows installer (tauri.conf.json, bundle.windows.nsis.installerHooks), inserted into Tauri's
; NSIS template. Saved as UTF-8 with a BOM: makensis reads the accented messages right only with it.

; The installer is per user (installMode currentUser): it runs without administrator rights, so in a folder the
; user may not write in (C:\Program Files, typed on the directory page) NSIS would stop at its bare «Error
; opening file for writing», with no reason given. The folder is tried before any file is copied: the user is
; told why it cannot be used and offered the per-user one, which is where the installer would have put it.
!macro NSIS_HOOK_PREINSTALL
  ClearErrors
  CreateDirectory "$INSTDIR"
  FileOpen $R0 "$INSTDIR\.capriole-write-test" w
  ${If} ${Errors}
    ${If} $INSTDIR != "$LOCALAPPDATA\${PRODUCTNAME}"
      !insertmacro CaprioleFolderMessage "$INSTDIR" "$LOCALAPPDATA\${PRODUCTNAME}"
      MessageBox MB_YESNO|MB_ICONEXCLAMATION "$R1" /SD IDYES IDNO capriole_abort
      StrCpy $INSTDIR "$LOCALAPPDATA\${PRODUCTNAME}"
      ClearErrors
      CreateDirectory "$INSTDIR"
      SetOutPath "$INSTDIR"
      ${IfNot} ${Errors}
        Goto capriole_writable
      ${EndIf}
    ${EndIf}
    !insertmacro CaprioleFolderMessage "$INSTDIR" ""
    MessageBox MB_OK|MB_ICONSTOP "$R1" /SD IDOK
    capriole_abort:
    Abort
  ${Else}
    FileClose $R0
    Delete "$INSTDIR\.capriole-write-test"
  ${EndIf}
  capriole_writable:
  ClearErrors
!macroend

; $R1 ← why the folder cannot be used, in the installer's language; with an alternative, the question whether to
; install there instead. The template's languages: English, Italian, French, German, Spanish (LANGIDs).
!macro CaprioleFolderMessage folder alternative
  ${If} $LANGUAGE == 1040
    StrCpy $R1 "Non hai i permessi per scrivere nella cartella ${folder}.$\r$\n$\r$\nCapriole si installa solo per il tuo utente, senza diritti di amministratore: non può usare cartelle di sistema come C:\Program Files."
    StrCmp "${alternative}" "" +2
      StrCpy $R1 "$R1$\r$\n$\r$\nVuoi installarlo in ${alternative}?"
  ${ElseIf} $LANGUAGE == 1036
    StrCpy $R1 "Vous n’avez pas le droit d’écrire dans le dossier ${folder}.$\r$\n$\r$\nCapriole s’installe pour votre utilisateur seulement, sans droits d’administrateur : il ne peut pas utiliser les dossiers du système comme C:\Program Files."
    StrCmp "${alternative}" "" +2
      StrCpy $R1 "$R1$\r$\n$\r$\nL’installer dans ${alternative} ?"
  ${ElseIf} $LANGUAGE == 1031
    StrCpy $R1 "Sie dürfen nicht in den Ordner ${folder} schreiben.$\r$\n$\r$\nCapriole wird nur für Ihren Benutzer installiert, ohne Administratorrechte: Systemordner wie C:\Program Files kann es nicht verwenden."
    StrCmp "${alternative}" "" +2
      StrCpy $R1 "$R1$\r$\n$\r$\nStattdessen in ${alternative} installieren?"
  ${ElseIf} $LANGUAGE == 1034
    StrCpy $R1 "No tienes permiso para escribir en la carpeta ${folder}.$\r$\n$\r$\nCapriole se instala solo para tu usuario, sin derechos de administrador: no puede usar carpetas del sistema como C:\Program Files."
    StrCmp "${alternative}" "" +2
      StrCpy $R1 "$R1$\r$\n$\r$\n¿Instalarlo en ${alternative}?"
  ${Else}
    StrCpy $R1 "You may not write in the folder ${folder}.$\r$\n$\r$\nCapriole installs for your user only, without administrator rights: it cannot use system folders such as C:\Program Files."
    StrCmp "${alternative}" "" +2
      StrCpy $R1 "$R1$\r$\n$\r$\nInstall it in ${alternative} instead?"
  ${EndIf}
!macroend
