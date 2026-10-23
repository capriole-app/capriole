import { translations } from './index.svelte';

/** The landing (components/landing/): what a visitor reads before opening a file, and what search engines
 *  read. The claims follow the brief (MARKETING/capriole/LANDING-brief.md, «Affermazioni»): true today, no
 *  other product named, no legal promise. */
export const landingWords = translations(
  {
    sections: 'Sezioni',
    nav: {
      privacy: 'Privacy',
      scratch: 'Fatto da zero',
      features: 'Funzioni',
      faq: 'Domande',
      desktop: 'App desktop',
    },
    hero: {
      before: 'Modifica i PDF online,',
      struck: 'solo le pagine',
      after: 'anche il testo',
      subtitle: 'Gratis e open source. E neanche un byte del tuo documento lascia il tuo computer.',
      trust: ['Gratis', 'Open source', 'Nessun account'],
      shot: 'L’editor di Capriole: il paragrafo di un contratto riscritto nel suo font',
    },
    card: {
      drop: 'Trascina qui il tuo PDF',
      note: 'resta tutto qui',
      desktop: 'Oppure scarica l’app per Windows o Linux',
    },
    privacy: {
      eyebrow: 'Privacy',
      title: 'Neanche un byte del documento lascia il tuo computer.',
      lead: 'Il PDF si apre, si modifica e si salva nel tuo browser. Non lo carichiamo da nessuna parte, nemmeno sui nostri server. E non devi crederci sulla parola: il codice è aperto, chiunque può verificare cosa fa.',
      points: {
        upload: {
          title: 'Nessun upload',
          text: 'Il file non passa da un server. Lo vedi anche dal pannello di rete del browser.',
        },
        open: {
          title: 'Codice aperto, verificabile',
          text: 'Sai esattamente cosa succede al tuo file, perché puoi leggerlo.',
        },
        account: {
          title: 'Nessun account, nessun tracciamento',
          text: 'Niente e-mail, niente cookie. Le visite si contano in forma anonima.',
        },
      },
    },
    scratch: {
      eyebrow: 'Fatto da zero',
      title: 'Non è la copia di un altro programma. È scritto da zero, con l’AI, riga per riga.',
      lead: 'Capriole non nasce ricostruendo un software commerciale. Il motore che legge, disegna, modifica e firma i PDF è scritto da capo, e l’interfaccia è disegnata per Capriole: l’intelligenza artificiale è lo strumento di lavoro.',
      points: {
        engine: {
          title: 'Un motore PDF nuovo',
          text: 'Scritto da zero in C++23: lo stesso codice nel browser e sul computer. Per le parti standard (compressione, immagini, font, crittografia) librerie open source note, tutte elencate.',
        },
        interface: {
          title: 'Un’interfaccia nostra',
          text: 'Disegnata per Capriole, non presa in prestito da nessuno.',
        },
        license: {
          title: 'Lo usi senza pensieri',
          text: 'Licenza 0BSD: a casa, in ufficio, in azienda, senza chiedere permesso. E puoi leggere ogni riga su GitHub.',
        },
      },
      github: 'Il codice su GitHub',
      licenses: 'Licenze e librerie',
    },
    features: {
      eyebrow: 'Funzioni',
      title: 'Tutto quello che serve a un PDF.',
      items: {
        text: { title: 'Modifica il testo', text: 'Righe e paragrafi, nel font del documento.' },
        pages: { title: 'Pagine', text: 'Riordina, ruota, elimina, estrai, unisci.' },
        annotations: { title: 'Annotazioni', text: 'Evidenzia, note, penna, forme, anche sulle scansioni.' },
        forms: { title: 'Moduli', text: 'Compila i campi e salva.' },
        signature: { title: 'Firma digitale', text: 'PAdES con il tuo certificato; verifica delle firme.' },
        redaction: { title: 'Redaction vera', text: 'Toglie il contenuto dal file, non lo copre.' },
        pdfa: { title: 'PDF/A', text: 'Per l’archiviazione a lungo termine.' },
        attachments: { title: 'Allegati e fatture', text: 'Factur-X e ZUGFeRD, da leggere e da creare.' },
        repair: { title: 'File danneggiati', text: 'Si aprono lo stesso, e ti dice cosa ha riparato.' },
      },
    },
    gallery: {
      eyebrow: 'Schermate',
      title: 'Guarda prima di provare.',
      scenes: {
        text: {
          label: 'Testo',
          caption: 'Modifica testo › Paragrafo: le righe si ricompongono nel font del documento.',
        },
        pages: {
          label: 'Pagine',
          caption: 'Le miniature: trascina per riordinare, ruota, elimina, estrai; unisci un altro PDF.',
        },
        annotations: {
          label: 'Annotazioni',
          caption: 'Evidenzia, sottolinea, note, penna e forme; anche sulle scansioni.',
        },
        signature: {
          label: 'Firma',
          caption: 'Firma PAdES con il tuo certificato; le firme che c’erano restano valide.',
        },
      },
    },
    desktop: {
      eyebrow: 'App desktop',
      title: 'Serve di più? C’è anche per Windows e Linux.',
      lead: 'La stessa Capriole, installata: salva direttamente sul disco, lavora senza connessione, firma con la smart card e crea firme a lungo termine.',
      windows: 'Scarica per Windows',
      linux: 'Scarica per Linux',
      mac: 'Gratis anche questa. Su Mac usa la versione web.',
    },
    faq: {
      eyebrow: 'Domande',
      title: 'Domande frequenti',
      items: [
        { q: 'È gratis?', a: 'Sì, tutte le funzioni, senza account. Ed è open source.' },
        {
          q: 'Il mio file viene caricato da qualche parte?',
          a: 'No, neanche un byte. Il PDF si apre e si modifica nel tuo browser; non passa da nessun server, nemmeno dal nostro. Solo se aggiungi alla firma una marca temporale, al servizio che hai scelto va l’impronta (hash) della firma, non il documento.',
        },
        {
          q: 'Da dove viene Capriole?',
          a: 'È scritto da zero, con l’AI come strumento di lavoro: il motore PDF e l’interfaccia non sono ricavati da altri programmi. Per le parti standard usa librerie open source note, elencate in «Informazioni e licenze».',
        },
        {
          q: 'Posso usarlo in azienda?',
          a: 'Sì. La licenza è la 0BSD: puoi usarlo, copiarlo e modificarlo, anche per lavoro, senza chiedere nulla.',
        },
        {
          q: 'Posso modificare il testo di qualunque PDF?',
          a: 'Dei PDF che hanno testo vero, sì: righe e paragrafi. Le scansioni sono immagini: lì puoi annotare, non riscrivere.',
        },
        {
          q: 'La firma digitale è valida?',
          a: 'Capriole crea firme PAdES, lo standard europeo dei PDF firmati. Il valore legale dipende dal certificato con cui firmi.',
        },
      ],
      back: 'Hai un PDF da sistemare?',
      backLink: 'Torna su e trascinalo nella scheda ↑',
    },
  },
  {
    en: {
      sections: 'Sections',
      nav: {
        privacy: 'Privacy',
        scratch: 'Made from scratch',
        features: 'Features',
        faq: 'Questions',
        desktop: 'Desktop app',
      },
      hero: {
        before: 'Edit PDFs online,',
        struck: 'pages only',
        after: 'the text too',
        subtitle: 'Free and open source. And not a single byte of your document leaves your computer.',
        trust: ['Free', 'Open source', 'No account'],
        shot: 'The Capriole editor: a contract’s paragraph rewritten in its own font',
      },
      card: {
        drop: 'Drop your PDF here',
        note: 'it all stays here',
        desktop: 'Or get the app for Windows or Linux',
      },
      privacy: {
        eyebrow: 'Privacy',
        title: 'Not a single byte of the document leaves your computer.',
        lead: 'The PDF opens, is edited and is saved in your browser. We don’t upload it anywhere, not even to our own servers. And you don’t have to take our word for it: the code is open, anyone can check what it does.',
        points: {
          upload: {
            title: 'No upload',
            text: 'The file never goes through a server. Your browser’s network panel shows it too.',
          },
          open: {
            title: 'Open, verifiable code',
            text: 'You know exactly what happens to your file, because you can read it.',
          },
          account: {
            title: 'No account, no tracking',
            text: 'No e-mail, no cookies. Visits are counted anonymously.',
          },
        },
      },
      scratch: {
        eyebrow: 'Made from scratch',
        title: 'Not a copy of another program. Written from scratch, with AI, line by line.',
        lead: 'Capriole was not built by reconstructing commercial software. The engine that reads, draws, edits and signs PDFs is written from the ground up, and the interface is designed for Capriole: artificial intelligence is the tool of the trade.',
        points: {
          engine: {
            title: 'A new PDF engine',
            text: 'Written from scratch in C++23: the same code in the browser and on the computer. For the standard parts (compression, images, fonts, cryptography) well-known open source libraries, all listed.',
          },
          interface: {
            title: 'An interface of our own',
            text: 'Designed for Capriole, borrowed from no one.',
          },
          license: {
            title: 'Use it with peace of mind',
            text: '0BSD licence: at home, at the office, in your company, without asking permission. And you can read every line on GitHub.',
          },
        },
        github: 'The code on GitHub',
        licenses: 'Licences and libraries',
      },
      features: {
        eyebrow: 'Features',
        title: 'Everything a PDF needs.',
        items: {
          text: { title: 'Edit the text', text: 'Lines and paragraphs, in the document’s own font.' },
          pages: { title: 'Pages', text: 'Reorder, rotate, delete, extract, merge.' },
          annotations: { title: 'Annotations', text: 'Highlights, notes, pen, shapes, on scans too.' },
          forms: { title: 'Forms', text: 'Fill in the fields and save.' },
          signature: { title: 'Digital signature', text: 'PAdES with your certificate; signature check.' },
          redaction: { title: 'Real redaction', text: 'Removes the content from the file, doesn’t cover it.' },
          pdfa: { title: 'PDF/A', text: 'For long-term archiving.' },
          attachments: { title: 'Attachments and invoices', text: 'Factur-X and ZUGFeRD, to read and to create.' },
          repair: { title: 'Damaged files', text: 'They open anyway, and it tells you what it repaired.' },
        },
      },
      gallery: {
        eyebrow: 'Screenshots',
        title: 'Take a look before you try.',
        scenes: {
          text: {
            label: 'Text',
            caption: 'Edit text › Paragraph: the lines reflow in the document’s own font.',
          },
          pages: {
            label: 'Pages',
            caption: 'The thumbnails: drag to reorder, rotate, delete, extract; merge another PDF.',
          },
          annotations: {
            label: 'Annotations',
            caption: 'Highlight, underline, notes, pen and shapes; on scans too.',
          },
          signature: {
            label: 'Signature',
            caption: 'PAdES signature with your certificate; the signatures already there stay valid.',
          },
        },
      },
      desktop: {
        eyebrow: 'Desktop app',
        title: 'Need more? It’s on Windows and Linux too.',
        lead: 'The same Capriole, installed: it saves straight to the disk, works offline, signs with a smart card and creates long-term signatures.',
        windows: 'Download for Windows',
        linux: 'Download for Linux',
        mac: 'Free as well. On a Mac, use the web version.',
      },
      faq: {
        eyebrow: 'Questions',
        title: 'Frequently asked questions',
        items: [
          { q: 'Is it free?', a: 'Yes, every feature, with no account. And it’s open source.' },
          {
            q: 'Is my file uploaded anywhere?',
            a: 'No, not a single byte. The PDF opens and is edited in your browser; it goes through no server, not even ours. Only if you add a time stamp to a signature does the service you chose receive the signature’s fingerprint (hash), not the document.',
          },
          {
            q: 'Where does Capriole come from?',
            a: 'It is written from scratch, with AI as the tool of the trade: the PDF engine and the interface are not derived from other programs. For the standard parts it uses well-known open source libraries, listed in «About and licences».',
          },
          {
            q: 'Can I use it at work?',
            a: 'Yes. The licence is 0BSD: you can use, copy and modify it, for work too, without asking anything.',
          },
          {
            q: 'Can I edit the text of any PDF?',
            a: 'Of PDFs that have real text, yes: lines and paragraphs. Scans are images: there you can annotate, not rewrite.',
          },
          {
            q: 'Is the digital signature valid?',
            a: 'Capriole creates PAdES signatures, the European standard for signed PDFs. Their legal value depends on the certificate you sign with.',
          },
        ],
        back: 'Got a PDF to fix?',
        backLink: 'Go back up and drop it on the card ↑',
      },
    },
    fr: {
      sections: 'Sections',
      nav: {
        privacy: 'Confidentialité',
        scratch: 'Fait de zéro',
        features: 'Fonctions',
        faq: 'Questions',
        desktop: 'Application',
      },
      hero: {
        before: 'Modifiez vos PDF en ligne,',
        struck: 'les pages seulement',
        after: 'texte compris',
        subtitle: 'Gratuit et open source. Et pas un octet de votre document ne quitte votre ordinateur.',
        trust: ['Gratuit', 'Open source', 'Sans compte'],
        shot: 'L’éditeur Capriole : le paragraphe d’un contrat réécrit dans sa police',
      },
      card: {
        drop: 'Déposez votre PDF ici',
        note: 'tout reste ici',
        desktop: 'Ou téléchargez l’application pour Windows ou Linux',
      },
      privacy: {
        eyebrow: 'Confidentialité',
        title: 'Pas un octet du document ne quitte votre ordinateur.',
        lead: 'Le PDF s’ouvre, se modifie et s’enregistre dans votre navigateur. Nous ne l’envoyons nulle part, pas même sur nos serveurs. Et inutile de nous croire sur parole : le code est ouvert, chacun peut vérifier ce qu’il fait.',
        points: {
          upload: {
            title: 'Aucun envoi',
            text: 'Le fichier ne passe par aucun serveur. Le panneau réseau du navigateur le montre aussi.',
          },
          open: {
            title: 'Code ouvert, vérifiable',
            text: 'Vous savez exactement ce qui arrive à votre fichier, parce que vous pouvez le lire.',
          },
          account: {
            title: 'Sans compte, sans pistage',
            text: 'Ni e-mail, ni cookies. Les visites sont comptées de façon anonyme.',
          },
        },
      },
      scratch: {
        eyebrow: 'Fait de zéro',
        title: 'Ce n’est pas la copie d’un autre programme. Il est écrit de zéro, avec l’IA, ligne par ligne.',
        lead: 'Capriole n’est pas né de la reconstruction d’un logiciel commercial. Le moteur qui lit, dessine, modifie et signe les PDF est écrit de A à Z, et l’interface est dessinée pour Capriole : l’intelligence artificielle est l’outil de travail.',
        points: {
          engine: {
            title: 'Un nouveau moteur PDF',
            text: 'Écrit de zéro en C++23 : le même code dans le navigateur et sur l’ordinateur. Pour les parties standard (compression, images, polices, cryptographie), des bibliothèques open source connues, toutes listées.',
          },
          interface: {
            title: 'Une interface à nous',
            text: 'Dessinée pour Capriole, empruntée à personne.',
          },
          license: {
            title: 'Utilisez-le l’esprit tranquille',
            text: 'Licence 0BSD : à la maison, au bureau, en entreprise, sans demander la permission. Et chaque ligne se lit sur GitHub.',
          },
        },
        github: 'Le code sur GitHub',
        licenses: 'Licences et bibliothèques',
      },
      features: {
        eyebrow: 'Fonctions',
        title: 'Tout ce dont un PDF a besoin.',
        items: {
          text: { title: 'Modifier le texte', text: 'Lignes et paragraphes, dans la police du document.' },
          pages: { title: 'Pages', text: 'Réordonner, faire pivoter, supprimer, extraire, fusionner.' },
          annotations: { title: 'Annotations', text: 'Surlignage, notes, stylo, formes, même sur les scans.' },
          forms: { title: 'Formulaires', text: 'Remplissez les champs et enregistrez.' },
          signature: {
            title: 'Signature numérique',
            text: 'PAdES avec votre certificat ; vérification des signatures.',
          },
          redaction: { title: 'Vraie suppression', text: 'Retire le contenu du fichier, ne le masque pas.' },
          pdfa: { title: 'PDF/A', text: 'Pour l’archivage à long terme.' },
          attachments: { title: 'Pièces jointes et factures', text: 'Factur-X et ZUGFeRD, à lire et à créer.' },
          repair: { title: 'Fichiers endommagés', text: 'Ils s’ouvrent quand même, et il vous dit ce qu’il a réparé.' },
        },
      },
      gallery: {
        eyebrow: 'Captures',
        title: 'Regardez avant d’essayer.',
        scenes: {
          text: {
            label: 'Texte',
            caption: 'Modifier le texte › Paragraphe : les lignes se recomposent dans la police du document.',
          },
          pages: {
            label: 'Pages',
            caption:
              'Les miniatures : glissez pour réordonner, faites pivoter, supprimez, extrayez ; fusionnez un autre PDF.',
          },
          annotations: {
            label: 'Annotations',
            caption: 'Surligner, souligner, notes, stylo et formes ; même sur les scans.',
          },
          signature: {
            label: 'Signature',
            caption: 'Signature PAdES avec votre certificat ; les signatures déjà présentes restent valides.',
          },
        },
      },
      desktop: {
        eyebrow: 'Application',
        title: 'Besoin de plus ? Il existe aussi pour Windows et Linux.',
        lead: 'Le même Capriole, installé : il enregistre directement sur le disque, fonctionne hors ligne, signe avec une carte à puce et crée des signatures à long terme.',
        windows: 'Télécharger pour Windows',
        linux: 'Télécharger pour Linux',
        mac: 'Gratuite elle aussi. Sur Mac, utilisez la version web.',
      },
      faq: {
        eyebrow: 'Questions',
        title: 'Questions fréquentes',
        items: [
          { q: 'Est-ce gratuit ?', a: 'Oui, toutes les fonctions, sans compte. Et c’est open source.' },
          {
            q: 'Mon fichier est-il envoyé quelque part ?',
            a: 'Non, pas un octet. Le PDF s’ouvre et se modifie dans votre navigateur ; il ne passe par aucun serveur, pas même le nôtre. Seulement si vous ajoutez un horodatage à une signature, le service choisi reçoit l’empreinte (hash) de la signature, pas le document.',
          },
          {
            q: 'D’où vient Capriole ?',
            a: 'Il est écrit de zéro, avec l’IA comme outil de travail : le moteur PDF et l’interface ne sont pas tirés d’autres programmes. Pour les parties standard, il utilise des bibliothèques open source connues, listées dans « À propos et licences ».',
          },
          {
            q: 'Puis-je l’utiliser en entreprise ?',
            a: 'Oui. La licence est la 0BSD : vous pouvez l’utiliser, le copier et le modifier, aussi pour le travail, sans rien demander.',
          },
          {
            q: 'Puis-je modifier le texte de n’importe quel PDF ?',
            a: 'Des PDF qui contiennent du vrai texte, oui : lignes et paragraphes. Les scans sont des images : on peut les annoter, pas les réécrire.',
          },
          {
            q: 'La signature numérique est-elle valable ?',
            a: 'Capriole crée des signatures PAdES, la norme européenne des PDF signés. Leur valeur juridique dépend du certificat avec lequel vous signez.',
          },
        ],
        back: 'Un PDF à corriger ?',
        backLink: 'Remontez et déposez-le sur la carte ↑',
      },
    },
    de: {
      sections: 'Abschnitte',
      nav: {
        privacy: 'Datenschutz',
        scratch: 'Von Grund auf',
        features: 'Funktionen',
        faq: 'Fragen',
        desktop: 'Desktop-App',
      },
      hero: {
        before: 'PDFs online bearbeiten,',
        struck: 'nur die Seiten',
        after: 'auch den Text',
        subtitle: 'Kostenlos und Open Source. Und kein einziges Byte Ihres Dokuments verlässt Ihren Computer.',
        trust: ['Kostenlos', 'Open Source', 'Kein Konto'],
        shot: 'Der Capriole-Editor: ein Vertragsabsatz, in seiner eigenen Schrift umgeschrieben',
      },
      card: {
        drop: 'PDF hierher ziehen',
        note: 'alles bleibt hier',
        desktop: 'Oder die App für Windows oder Linux herunterladen',
      },
      privacy: {
        eyebrow: 'Datenschutz',
        title: 'Kein einziges Byte des Dokuments verlässt Ihren Computer.',
        lead: 'Die PDF wird in Ihrem Browser geöffnet, bearbeitet und gespeichert. Wir laden sie nirgendwohin hoch, auch nicht auf unsere Server. Und Sie müssen uns nicht beim Wort nehmen: Der Code ist offen, jeder kann prüfen, was er tut.',
        points: {
          upload: {
            title: 'Kein Upload',
            text: 'Die Datei geht über keinen Server. Das zeigt auch das Netzwerk-Panel des Browsers.',
          },
          open: {
            title: 'Offener, überprüfbarer Code',
            text: 'Sie wissen genau, was mit Ihrer Datei geschieht, denn Sie können es nachlesen.',
          },
          account: {
            title: 'Kein Konto, kein Tracking',
            text: 'Keine E-Mail, keine Cookies. Besuche werden anonym gezählt.',
          },
        },
      },
      scratch: {
        eyebrow: 'Von Grund auf',
        title: 'Keine Kopie eines anderen Programms. Von Grund auf geschrieben, mit KI, Zeile für Zeile.',
        lead: 'Capriole ist nicht durch den Nachbau einer kommerziellen Software entstanden. Die Engine, die PDFs liest, darstellt, bearbeitet und signiert, ist von Grund auf neu geschrieben, und die Oberfläche ist für Capriole gestaltet: Künstliche Intelligenz ist das Werkzeug.',
        points: {
          engine: {
            title: 'Eine neue PDF-Engine',
            text: 'Von Grund auf in C++23 geschrieben: derselbe Code im Browser und auf dem Computer. Für die Standardteile (Kompression, Bilder, Schriften, Kryptografie) bekannte Open-Source-Bibliotheken, alle aufgeführt.',
          },
          interface: {
            title: 'Eine eigene Oberfläche',
            text: 'Für Capriole gestaltet, von niemandem geliehen.',
          },
          license: {
            title: 'Unbesorgt nutzen',
            text: '0BSD-Lizenz: zu Hause, im Büro, im Unternehmen, ohne um Erlaubnis zu fragen. Und jede Zeile ist auf GitHub zu lesen.',
          },
        },
        github: 'Der Code auf GitHub',
        licenses: 'Lizenzen und Bibliotheken',
      },
      features: {
        eyebrow: 'Funktionen',
        title: 'Alles, was eine PDF braucht.',
        items: {
          text: { title: 'Text bearbeiten', text: 'Zeilen und Absätze, in der Schrift des Dokuments.' },
          pages: { title: 'Seiten', text: 'Neu ordnen, drehen, löschen, extrahieren, zusammenführen.' },
          annotations: { title: 'Anmerkungen', text: 'Markieren, Notizen, Stift, Formen, auch auf Scans.' },
          forms: { title: 'Formulare', text: 'Felder ausfüllen und speichern.' },
          signature: { title: 'Digitale Signatur', text: 'PAdES mit Ihrem Zertifikat; Prüfung der Signaturen.' },
          redaction: { title: 'Echtes Schwärzen', text: 'Entfernt den Inhalt aus der Datei, statt ihn abzudecken.' },
          pdfa: { title: 'PDF/A', text: 'Für die Langzeitarchivierung.' },
          attachments: { title: 'Anhänge und Rechnungen', text: 'Factur-X und ZUGFeRD, zum Lesen und Erstellen.' },
          repair: {
            title: 'Beschädigte Dateien',
            text: 'Sie öffnen sich trotzdem, und es sagt Ihnen, was repariert wurde.',
          },
        },
      },
      gallery: {
        eyebrow: 'Bildschirmfotos',
        title: 'Erst ansehen, dann ausprobieren.',
        scenes: {
          text: {
            label: 'Text',
            caption: 'Text bearbeiten › Absatz: Die Zeilen setzen sich in der Schrift des Dokuments neu.',
          },
          pages: {
            label: 'Seiten',
            caption: 'Die Miniaturen: ziehen zum Neuordnen, drehen, löschen, extrahieren; eine weitere PDF anfügen.',
          },
          annotations: {
            label: 'Anmerkungen',
            caption: 'Markieren, unterstreichen, Notizen, Stift und Formen; auch auf Scans.',
          },
          signature: {
            label: 'Signatur',
            caption: 'PAdES-Signatur mit Ihrem Zertifikat; vorhandene Signaturen bleiben gültig.',
          },
        },
      },
      desktop: {
        eyebrow: 'Desktop-App',
        title: 'Mehr gebraucht? Es gibt sie auch für Windows und Linux.',
        lead: 'Dasselbe Capriole, installiert: Es speichert direkt auf der Festplatte, arbeitet offline, signiert mit der Chipkarte und erstellt Langzeitsignaturen.',
        windows: 'Für Windows herunterladen',
        linux: 'Für Linux herunterladen',
        mac: 'Auch sie kostenlos. Auf dem Mac nutzen Sie die Web-Version.',
      },
      faq: {
        eyebrow: 'Fragen',
        title: 'Häufige Fragen',
        items: [
          { q: 'Ist es kostenlos?', a: 'Ja, alle Funktionen, ohne Konto. Und es ist Open Source.' },
          {
            q: 'Wird meine Datei irgendwo hochgeladen?',
            a: 'Nein, kein einziges Byte. Die PDF wird in Ihrem Browser geöffnet und bearbeitet; sie geht über keinen Server, auch nicht über unseren. Nur wenn Sie einer Signatur einen Zeitstempel hinzufügen, erhält der gewählte Dienst den Fingerabdruck (Hash) der Signatur, nicht das Dokument.',
          },
          {
            q: 'Woher kommt Capriole?',
            a: 'Es ist von Grund auf geschrieben, mit KI als Werkzeug: Die PDF-Engine und die Oberfläche sind nicht aus anderen Programmen abgeleitet. Für die Standardteile nutzt es bekannte Open-Source-Bibliotheken, aufgeführt unter „Info und Lizenzen“.',
          },
          {
            q: 'Darf ich es im Unternehmen nutzen?',
            a: 'Ja. Die Lizenz ist 0BSD: Sie dürfen es nutzen, kopieren und ändern, auch beruflich, ohne etwas zu fragen.',
          },
          {
            q: 'Kann ich den Text jeder PDF bearbeiten?',
            a: 'Bei PDFs mit echtem Text, ja: Zeilen und Absätze. Scans sind Bilder: Dort können Sie Anmerkungen setzen, aber nicht umschreiben.',
          },
          {
            q: 'Ist die digitale Signatur gültig?',
            a: 'Capriole erstellt PAdES-Signaturen, den europäischen Standard für signierte PDFs. Ihr rechtlicher Wert hängt vom Zertifikat ab, mit dem Sie signieren.',
          },
        ],
        back: 'Eine PDF zu korrigieren?',
        backLink: 'Nach oben und auf die Karte ziehen ↑',
      },
    },
    es: {
      sections: 'Secciones',
      nav: {
        privacy: 'Privacidad',
        scratch: 'Hecho desde cero',
        features: 'Funciones',
        faq: 'Preguntas',
        desktop: 'App de escritorio',
      },
      hero: {
        before: 'Edita PDF en línea,',
        struck: 'solo las páginas',
        after: 'también el texto',
        subtitle: 'Gratis y de código abierto. Y ni un byte de tu documento sale de tu ordenador.',
        trust: ['Gratis', 'Código abierto', 'Sin cuenta'],
        shot: 'El editor de Capriole: el párrafo de un contrato reescrito en su propia fuente',
      },
      card: {
        drop: 'Arrastra aquí tu PDF',
        note: 'todo se queda aquí',
        desktop: 'O descarga la app para Windows o Linux',
      },
      privacy: {
        eyebrow: 'Privacidad',
        title: 'Ni un byte del documento sale de tu ordenador.',
        lead: 'El PDF se abre, se edita y se guarda en tu navegador. No lo subimos a ningún sitio, ni siquiera a nuestros servidores. Y no tienes que creernos: el código es abierto, cualquiera puede comprobar lo que hace.',
        points: {
          upload: {
            title: 'Sin subidas',
            text: 'El archivo no pasa por ningún servidor. También lo ves en el panel de red del navegador.',
          },
          open: {
            title: 'Código abierto, verificable',
            text: 'Sabes exactamente qué pasa con tu archivo, porque puedes leerlo.',
          },
          account: {
            title: 'Sin cuenta, sin rastreo',
            text: 'Ni e-mail, ni cookies. Las visitas se cuentan de forma anónima.',
          },
        },
      },
      scratch: {
        eyebrow: 'Hecho desde cero',
        title: 'No es la copia de otro programa. Está escrito desde cero, con IA, línea a línea.',
        lead: 'Capriole no nace de reconstruir un software comercial. El motor que lee, dibuja, edita y firma los PDF está escrito desde el principio, y la interfaz está diseñada para Capriole: la inteligencia artificial es la herramienta de trabajo.',
        points: {
          engine: {
            title: 'Un motor PDF nuevo',
            text: 'Escrito desde cero en C++23: el mismo código en el navegador y en el ordenador. Para las partes estándar (compresión, imágenes, fuentes, criptografía), bibliotecas de código abierto conocidas, todas enumeradas.',
          },
          interface: {
            title: 'Una interfaz propia',
            text: 'Diseñada para Capriole, no tomada prestada de nadie.',
          },
          license: {
            title: 'Úsalo con tranquilidad',
            text: 'Licencia 0BSD: en casa, en la oficina, en la empresa, sin pedir permiso. Y puedes leer cada línea en GitHub.',
          },
        },
        github: 'El código en GitHub',
        licenses: 'Licencias y bibliotecas',
      },
      features: {
        eyebrow: 'Funciones',
        title: 'Todo lo que necesita un PDF.',
        items: {
          text: { title: 'Editar el texto', text: 'Líneas y párrafos, en la fuente del documento.' },
          pages: { title: 'Páginas', text: 'Reordena, gira, elimina, extrae, une.' },
          annotations: { title: 'Anotaciones', text: 'Resalta, notas, lápiz, formas, también en escaneos.' },
          forms: { title: 'Formularios', text: 'Rellena los campos y guarda.' },
          signature: { title: 'Firma digital', text: 'PAdES con tu certificado; verificación de firmas.' },
          redaction: { title: 'Ocultación real', text: 'Quita el contenido del archivo, no lo tapa.' },
          pdfa: { title: 'PDF/A', text: 'Para el archivo a largo plazo.' },
          attachments: { title: 'Adjuntos y facturas', text: 'Factur-X y ZUGFeRD, para leer y para crear.' },
          repair: { title: 'Archivos dañados', text: 'Se abren igualmente, y te dice qué ha reparado.' },
        },
      },
      gallery: {
        eyebrow: 'Capturas',
        title: 'Echa un vistazo antes de probar.',
        scenes: {
          text: {
            label: 'Texto',
            caption: 'Editar texto › Párrafo: las líneas se recomponen en la fuente del documento.',
          },
          pages: {
            label: 'Páginas',
            caption: 'Las miniaturas: arrastra para reordenar, gira, elimina, extrae; une otro PDF.',
          },
          annotations: {
            label: 'Anotaciones',
            caption: 'Resalta, subraya, notas, lápiz y formas; también en escaneos.',
          },
          signature: {
            label: 'Firma',
            caption: 'Firma PAdES con tu certificado; las firmas que ya había siguen siendo válidas.',
          },
        },
      },
      desktop: {
        eyebrow: 'App de escritorio',
        title: '¿Necesitas más? También está para Windows y Linux.',
        lead: 'La misma Capriole, instalada: guarda directamente en el disco, funciona sin conexión, firma con tarjeta inteligente y crea firmas a largo plazo.',
        windows: 'Descargar para Windows',
        linux: 'Descargar para Linux',
        mac: 'También gratis. En Mac, usa la versión web.',
      },
      faq: {
        eyebrow: 'Preguntas',
        title: 'Preguntas frecuentes',
        items: [
          { q: '¿Es gratis?', a: 'Sí, todas las funciones, sin cuenta. Y es de código abierto.' },
          {
            q: '¿Mi archivo se sube a algún sitio?',
            a: 'No, ni un byte. El PDF se abre y se edita en tu navegador; no pasa por ningún servidor, ni siquiera el nuestro. Solo si añades un sello de tiempo a una firma, el servicio que elegiste recibe la huella (hash) de la firma, no el documento.',
          },
          {
            q: '¿De dónde viene Capriole?',
            a: 'Está escrito desde cero, con la IA como herramienta de trabajo: el motor PDF y la interfaz no derivan de otros programas. Para las partes estándar usa bibliotecas de código abierto conocidas, enumeradas en «Información y licencias».',
          },
          {
            q: '¿Puedo usarlo en la empresa?',
            a: 'Sí. La licencia es la 0BSD: puedes usarlo, copiarlo y modificarlo, también para trabajar, sin pedir nada.',
          },
          {
            q: '¿Puedo editar el texto de cualquier PDF?',
            a: 'De los PDF que tienen texto real, sí: líneas y párrafos. Los escaneos son imágenes: ahí puedes anotar, no reescribir.',
          },
          {
            q: '¿La firma digital es válida?',
            a: 'Capriole crea firmas PAdES, el estándar europeo de los PDF firmados. Su valor legal depende del certificado con el que firmes.',
          },
        ],
        back: '¿Tienes un PDF que arreglar?',
        backLink: 'Vuelve arriba y arrástralo a la tarjeta ↑',
      },
    },
  },
);
