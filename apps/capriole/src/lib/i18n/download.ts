import { translations } from './index.svelte';

/** «Scarica l'app per il computer» (components/DownloadDialog.svelte), only in the web app. */
export const downloadWords = translations(
  {
    open: 'Scarica l’app per il computer',
    title: 'Capriole per il computer',
    intro:
      'La stessa app, installata: funziona anche senza rete, apre e salva i file direttamente sul disco e in più firma con la smart card (CNS, CIE, firma qualificata) e con la validazione a lungo termine.',
    windows: 'Windows 10 e 11 (64 bit)',
    windowsFile: 'installer, per il tuo utente',
    unsigned:
      'L’installer non è ancora firmato: Windows può avvisare che l’editore è sconosciuto. Scegli «Ulteriori informazioni», poi «Esegui comunque».',
    linux: 'Linux (64 bit)',
    deb: 'Debian, Ubuntu e derivate',
    appImage: 'le altre distribuzioni',
    checksums: 'Impronte SHA-256',
    allReleases: 'Tutte le versioni e le note',
    close: 'Chiudi',
  },
  {
    en: {
      open: 'Get the app for your computer',
      title: 'Capriole for your computer',
      intro:
        'The same app, installed: it works offline too, opens and saves files straight on the disk, and also signs with a smart card (national ID cards, qualified signatures) and with long-term validation.',
      windows: 'Windows 10 and 11 (64-bit)',
      windowsFile: 'installer, for your user',
      unsigned:
        'The installer is not signed yet: Windows may warn that the publisher is unknown. Choose «More info», then «Run anyway».',
      linux: 'Linux (64-bit)',
      deb: 'Debian, Ubuntu and derivatives',
      appImage: 'other distributions',
      checksums: 'SHA-256 checksums',
      allReleases: 'All versions and release notes',
      close: 'Close',
    },
    fr: {
      open: 'Télécharger l’application pour ordinateur',
      title: 'Capriole pour ordinateur',
      intro:
        'La même application, installée : elle fonctionne aussi hors ligne, ouvre et enregistre les fichiers directement sur le disque, et signe en plus avec une carte à puce (cartes d’identité, signature qualifiée) et avec la validation à long terme.',
      windows: 'Windows 10 et 11 (64 bits)',
      windowsFile: 'programme d’installation, pour votre utilisateur',
      unsigned:
        'Le programme d’installation n’est pas encore signé : Windows peut signaler un éditeur inconnu. Choisissez « Informations complémentaires », puis « Exécuter quand même ».',
      linux: 'Linux (64 bits)',
      deb: 'Debian, Ubuntu et dérivées',
      appImage: 'les autres distributions',
      checksums: 'Empreintes SHA-256',
      allReleases: 'Toutes les versions et les notes',
      close: 'Fermer',
    },
    de: {
      open: 'App für den Computer herunterladen',
      title: 'Capriole für den Computer',
      intro:
        'Dieselbe App, installiert: Sie funktioniert auch offline, öffnet und speichert Dateien direkt auf der Festplatte und signiert zusätzlich mit einer Chipkarte (Ausweiskarten, qualifizierte Signatur) und mit Langzeitvalidierung.',
      windows: 'Windows 10 und 11 (64 Bit)',
      windowsFile: 'Installationsprogramm, für Ihren Benutzer',
      unsigned:
        'Das Installationsprogramm ist noch nicht signiert: Windows kann vor einem unbekannten Herausgeber warnen. Wählen Sie „Weitere Informationen“, dann „Trotzdem ausführen“.',
      linux: 'Linux (64 Bit)',
      deb: 'Debian, Ubuntu und Derivate',
      appImage: 'andere Distributionen',
      checksums: 'SHA-256-Prüfsummen',
      allReleases: 'Alle Versionen und Versionshinweise',
      close: 'Schließen',
    },
    es: {
      open: 'Descargar la app para el ordenador',
      title: 'Capriole para el ordenador',
      intro:
        'La misma app, instalada: funciona también sin conexión, abre y guarda los archivos directamente en el disco y además firma con tarjeta inteligente (DNI electrónico, firma cualificada) y con validación a largo plazo.',
      windows: 'Windows 10 y 11 (64 bits)',
      windowsFile: 'instalador, para tu usuario',
      unsigned:
        'El instalador aún no está firmado: Windows puede avisar de que el editor es desconocido. Elige «Más información» y luego «Ejecutar de todas formas».',
      linux: 'Linux (64 bits)',
      deb: 'Debian, Ubuntu y derivadas',
      appImage: 'las demás distribuciones',
      checksums: 'Huellas SHA-256',
      allReleases: 'Todas las versiones y notas',
      close: 'Cerrar',
    },
  },
);
