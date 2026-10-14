import { translations } from './index.svelte';

// The terms of use (components/TermsDialog.svelte) and the words that ask to accept them (OpenCard). A legal
// text: the English one prevails, and each says so. Changing what they say: change `termsVersion` in
// lib/terms-version.ts too, so that everyone is asked to accept them again.

export interface Section {
  heading: string;
  body: string[];
}
/** Who offers Capriole (lib/terms-version.ts). */
export interface Provider {
  name: string;
  contact: string;
}

export const termsWords = translations(
  {
    title: 'Condizioni d’uso',
    updated: (date: string) => `Versione del ${date}`,
    sections: (who: Provider): Section[] => [
      {
        heading: 'Queste condizioni',
        body: [
          `Regolano l’uso di Capriole, l’app web su capriole.app e l’app desktop, offerto da ${who.name} (contatto: ${who.contact}). Usando Capriole le accetti; se non le accetti, non usarlo.`,
        ],
      },
      {
        heading: 'Cos’è Capriole',
        body: [
          'Un programma gratuito per modificare i PDF, software libero con licenza 0BSD. I documenti sono elaborati sul tuo dispositivo: non vengono inviati né a noi né ad altri, e noi non li vediamo né li conserviamo.',
        ],
      },
      {
        heading: 'Nessuna garanzia',
        body: [
          'Capriole è fornito «così com’è» e «come disponibile», senza garanzie di alcun tipo, esplicite o implicite, comprese quelle di commerciabilità, di idoneità a uno scopo particolare e di assenza di errori.',
          'In particolare non garantiamo che un file sia aperto, modificato o salvato correttamente; che il risultato sia accettato da altri programmi, enti o controparti; che firme digitali, documenti PDF/A, oscuramenti e moduli abbiano valore legale o rispettino requisiti di legge; che il servizio sia sempre disponibile.',
        ],
      },
      {
        heading: 'A cosa devi pensare tu',
        body: [
          'Usi Capriole a tuo rischio. Prima di modificare un documento, conservane una copia. Controlla il risultato prima di usarlo, inviarlo o firmarlo. Salvando sopra un file esistente lo sostituisci, e l’originale non si recupera. Dopo un oscuramento, verifica che il contenuto sia davvero sparito.',
          'Sei responsabile dei documenti che elabori, di avere il diritto di modificarli e dell’uso che ne fai, che deve essere lecito.',
        ],
      },
      {
        heading: 'Limitazione di responsabilità',
        body: [
          `Nella misura massima consentita dalla legge, né ${who.name} né chi contribuisce a Capriole risponde di danni di alcun tipo, diretti o indiretti, che derivino dall’uso di Capriole o dall’impossibilità di usarlo: tra questi la perdita, l’alterazione o il danneggiamento di file e dati (anche salvando sopra un file), documenti rifiutati, non validi o non conformi, mancati guadagni, interruzioni di attività e pretese di terzi. Capriole è gratuito, e questa limitazione è una condizione essenziale per offrirlo senza compenso.`,
          'Nulla in queste condizioni esclude o limita una responsabilità che la legge non permette di escludere o limitare, come quella per dolo o colpa grave o per morte o lesioni personali causate da negligenza, né i diritti che la legge riconosce ai consumatori senza possibilità di rinuncia.',
        ],
      },
      {
        heading: 'Privacy',
        body: [
          'Non raccogliamo i tuoi documenti; niente cookie di tracciamento né statistiche di utilizzo. Le preferenze (lingua, tema, impostazioni, l’accettazione di queste condizioni) restano sul tuo dispositivo, nella memoria del browser o dell’app.',
          'Non registriamo indirizzi IP: contiamo solo le aperture della pagina, in forma anonima (il giorno e il tipo di browser, per distinguere i robot dei motori di ricerca). L’app desktop si collega alla rete solo quando lo chiedi tu, per esempio a un servizio di marca temporale o di verifica della revoca per una firma.',
        ],
      },
      {
        heading: 'Modifiche',
        body: [
          'Possiamo aggiornare queste condizioni. Vale la versione pubblicata nell’app, con la sua data; quando cambiano, ti chiediamo di accettarle di nuovo.',
        ],
      },
      {
        heading: 'Legge applicabile e foro',
        body: [
          'Queste condizioni sono regolate dalla legge irlandese, e per le controversie sono competenti i tribunali d’Irlanda. Se sei un consumatore, conservi la protezione delle norme inderogabili del paese in cui risiedi e puoi rivolgerti ai suoi giudici. Se una clausola risulta invalida, le altre restano valide.',
        ],
      },
    ],
    prevails: 'Queste condizioni sono tradotte in più lingue: in caso di differenze prevale la versione inglese.',
    close: 'Chiudi',
    acceptBefore: 'Ho letto e accetto le',
    acceptLink: 'condizioni d’uso',
    acceptAfter: '',
    mustAccept: 'Per aprire un file, accetta prima le condizioni d’uso.',
  },
  {
    en: {
      title: 'Terms of use',
      updated: (date: string) => `Version of ${date}`,
      sections: (who: Provider): Section[] => [
        {
          heading: 'These terms',
          body: [
            `They govern the use of Capriole, the web app at capriole.app and the desktop app, offered by ${who.name} (contact: ${who.contact}). By using Capriole you accept them; if you do not accept them, do not use it.`,
          ],
        },
        {
          heading: 'What Capriole is',
          body: [
            'A free program for editing PDFs, free software under the 0BSD licence. Documents are processed on your device: they are not sent to us or to anyone else, and we neither see nor keep them.',
          ],
        },
        {
          heading: 'No warranty',
          body: [
            'Capriole is provided “as is” and “as available”, without warranties of any kind, express or implied, including those of merchantability, fitness for a particular purpose and freedom from errors.',
            'In particular, we do not warrant that a file will be opened, edited or saved correctly; that the result will be accepted by other programs, authorities or counterparties; that digital signatures, PDF/A documents, redactions and forms will have legal effect or meet legal requirements; or that the service will always be available.',
          ],
        },
        {
          heading: 'What is up to you',
          body: [
            'You use Capriole at your own risk. Before editing a document, keep a copy of it. Check the result before you use, send or sign it. Saving over an existing file replaces it, and the original cannot be recovered. After a redaction, check that the content is really gone.',
            'You are responsible for the documents you process, for having the right to edit them, and for the use you make of them, which must be lawful.',
          ],
        },
        {
          heading: 'Limitation of liability',
          body: [
            `To the fullest extent permitted by law, neither ${who.name} nor the contributors to Capriole are liable for damage of any kind, direct or indirect, arising from the use of Capriole or the inability to use it, including the loss, alteration or corruption of files and data (also by saving over a file), documents rejected, invalid or non-compliant, loss of profits, business interruption and claims by third parties. Capriole is free of charge, and this limitation is an essential condition of offering it without payment.`,
            'Nothing in these terms excludes or limits any liability that cannot be excluded or limited by law, such as liability for fraud or gross negligence or for death or personal injury caused by negligence, nor the rights that the law gives consumers and that cannot be waived.',
          ],
        },
        {
          heading: 'Privacy',
          body: [
            'We do not collect your documents; there are no tracking cookies and no usage statistics. Your preferences (language, theme, settings, the acceptance of these terms) stay on your device, in the browser’s or the app’s storage.',
            'We do not log IP addresses: we only count how often the page is opened, anonymously (the day and the kind of browser, to tell search engines’ robots apart). The desktop app connects to the network only when you ask it to, for example to a time-stamping or revocation-checking service for a signature.',
          ],
        },
        {
          heading: 'Changes',
          body: [
            'We may update these terms. The version published in the app, with its date, applies; when they change, we ask you to accept them again.',
          ],
        },
        {
          heading: 'Governing law and jurisdiction',
          body: [
            'These terms are governed by the laws of Ireland, and the courts of Ireland have jurisdiction over disputes. If you are a consumer, you keep the protection of the mandatory rules of the country where you live and may bring proceedings before its courts. If a provision is found invalid, the others remain in force.',
          ],
        },
      ],
      prevails: 'These terms are translated into several languages: if they differ, the English version prevails.',
      close: 'Close',
      acceptBefore: 'I have read and accept the',
      acceptLink: 'terms of use',
      acceptAfter: '',
      mustAccept: 'To open a file, first accept the terms of use.',
    },
    fr: {
      title: 'Conditions d’utilisation',
      updated: (date: string) => `Version du ${date}`,
      sections: (who: Provider): Section[] => [
        {
          heading: 'Ces conditions',
          body: [
            `Elles régissent l’utilisation de Capriole, l’application web sur capriole.app et l’application de bureau, proposé par ${who.name} (contact : ${who.contact}). En utilisant Capriole, vous les acceptez ; si vous ne les acceptez pas, ne l’utilisez pas.`,
          ],
        },
        {
          heading: 'Ce qu’est Capriole',
          body: [
            'Un programme gratuit pour modifier les PDF, logiciel libre sous licence 0BSD. Les documents sont traités sur votre appareil : ils ne sont envoyés ni à nous ni à personne, et nous ne les voyons ni ne les conservons.',
          ],
        },
        {
          heading: 'Aucune garantie',
          body: [
            'Capriole est fourni « tel quel » et « selon disponibilité », sans garantie d’aucune sorte, expresse ou implicite, y compris de qualité marchande, d’adéquation à un usage particulier et d’absence d’erreurs.',
            'En particulier, nous ne garantissons pas qu’un fichier soit ouvert, modifié ou enregistré correctement ; que le résultat soit accepté par d’autres programmes, administrations ou contreparties ; que les signatures électroniques, les documents PDF/A, les caviardages et les formulaires aient une valeur juridique ou respectent des exigences légales ; ni que le service soit toujours disponible.',
          ],
        },
        {
          heading: 'Ce qui vous revient',
          body: [
            'Vous utilisez Capriole à vos propres risques. Avant de modifier un document, conservez-en une copie. Vérifiez le résultat avant de l’utiliser, de l’envoyer ou de le signer. Enregistrer par-dessus un fichier existant le remplace, et l’original ne peut pas être récupéré. Après un caviardage, vérifiez que le contenu a bien disparu.',
            'Vous êtes responsable des documents que vous traitez, du droit de les modifier et de l’usage que vous en faites, qui doit être licite.',
          ],
        },
        {
          heading: 'Limitation de responsabilité',
          body: [
            `Dans toute la mesure permise par la loi, ni ${who.name} ni les contributeurs de Capriole ne sont responsables des dommages de toute nature, directs ou indirects, résultant de l’utilisation de Capriole ou de l’impossibilité de l’utiliser, notamment la perte, l’altération ou la corruption de fichiers et de données (y compris en enregistrant par-dessus un fichier), les documents refusés, invalides ou non conformes, le manque à gagner, l’interruption d’activité et les réclamations de tiers. Capriole est gratuit, et cette limitation est une condition essentielle pour le proposer sans contrepartie.`,
            'Rien dans ces conditions n’exclut ni ne limite une responsabilité que la loi ne permet pas d’exclure ou de limiter, comme celle pour dol ou faute lourde ou pour décès ou dommages corporels causés par négligence, ni les droits que la loi reconnaît aux consommateurs sans possibilité d’y renoncer.',
          ],
        },
        {
          heading: 'Vie privée',
          body: [
            'Nous ne collectons pas vos documents ; ni cookies de suivi ni statistiques d’utilisation. Vos préférences (langue, thème, réglages, l’acceptation de ces conditions) restent sur votre appareil, dans la mémoire du navigateur ou de l’application.',
            'Nous n’enregistrons pas les adresses IP : nous comptons seulement les ouvertures de la page, de façon anonyme (le jour et le type de navigateur, pour distinguer les robots des moteurs de recherche). L’application de bureau ne se connecte au réseau que lorsque vous le demandez, par exemple à un service d’horodatage ou de vérification de révocation pour une signature.',
          ],
        },
        {
          heading: 'Modifications',
          body: [
            'Nous pouvons mettre à jour ces conditions. La version publiée dans l’application, avec sa date, s’applique ; lorsqu’elles changent, nous vous demandons de les accepter à nouveau.',
          ],
        },
        {
          heading: 'Droit applicable et juridiction',
          body: [
            'Ces conditions sont régies par le droit irlandais, et les tribunaux d’Irlande sont compétents pour les litiges. Si vous êtes un consommateur, vous conservez la protection des règles impératives du pays où vous résidez et pouvez saisir ses tribunaux. Si une clause est jugée invalide, les autres restent en vigueur.',
          ],
        },
      ],
      prevails:
        'Ces conditions sont traduites en plusieurs langues : en cas de divergence, la version anglaise prévaut.',
      close: 'Fermer',
      acceptBefore: 'J’ai lu et j’accepte les',
      acceptLink: 'conditions d’utilisation',
      acceptAfter: '',
      mustAccept: 'Pour ouvrir un fichier, acceptez d’abord les conditions d’utilisation.',
    },
    de: {
      title: 'Nutzungsbedingungen',
      updated: (date: string) => `Fassung vom ${date}`,
      sections: (who: Provider): Section[] => [
        {
          heading: 'Diese Bedingungen',
          body: [
            `Sie regeln die Nutzung von Capriole, der Web-App auf capriole.app und der Desktop-App, angeboten von ${who.name} (Kontakt: ${who.contact}). Mit der Nutzung von Capriole akzeptieren Sie sie; wenn Sie sie nicht akzeptieren, nutzen Sie Capriole nicht.`,
          ],
        },
        {
          heading: 'Was Capriole ist',
          body: [
            'Ein kostenloses Programm zum Bearbeiten von PDFs, freie Software unter der Lizenz 0BSD. Die Dokumente werden auf Ihrem Gerät verarbeitet: Sie werden weder an uns noch an andere gesendet, und wir sehen oder speichern sie nicht.',
          ],
        },
        {
          heading: 'Keine Gewährleistung',
          body: [
            'Capriole wird „wie besehen“ und „wie verfügbar“ bereitgestellt, ohne ausdrückliche oder stillschweigende Gewährleistung jeglicher Art, einschließlich der Marktgängigkeit, der Eignung für einen bestimmten Zweck und der Fehlerfreiheit.',
            'Insbesondere gewährleisten wir nicht, dass eine Datei korrekt geöffnet, bearbeitet oder gespeichert wird; dass das Ergebnis von anderen Programmen, Behörden oder Vertragspartnern angenommen wird; dass digitale Signaturen, PDF/A-Dokumente, Schwärzungen und Formulare rechtswirksam sind oder gesetzliche Anforderungen erfüllen; oder dass der Dienst stets verfügbar ist.',
          ],
        },
        {
          heading: 'Was bei Ihnen liegt',
          body: [
            'Sie nutzen Capriole auf eigenes Risiko. Bewahren Sie vor dem Bearbeiten eines Dokuments eine Kopie auf. Prüfen Sie das Ergebnis, bevor Sie es verwenden, versenden oder unterschreiben. Wer über eine vorhandene Datei speichert, ersetzt sie, und das Original lässt sich nicht wiederherstellen. Prüfen Sie nach einer Schwärzung, dass der Inhalt wirklich entfernt ist.',
            'Sie sind verantwortlich für die Dokumente, die Sie verarbeiten, für das Recht, sie zu bearbeiten, und für deren Verwendung, die rechtmäßig sein muss.',
          ],
        },
        {
          heading: 'Haftungsbeschränkung',
          body: [
            `Soweit gesetzlich zulässig, haften weder ${who.name} noch die Mitwirkenden an Capriole für Schäden jeglicher Art, unmittelbar oder mittelbar, die aus der Nutzung von Capriole oder der Unmöglichkeit der Nutzung entstehen, darunter Verlust, Veränderung oder Beschädigung von Dateien und Daten (auch durch Speichern über eine Datei), abgelehnte, ungültige oder nicht konforme Dokumente, entgangener Gewinn, Betriebsunterbrechungen und Ansprüche Dritter. Capriole ist kostenlos, und diese Beschränkung ist eine wesentliche Bedingung dafür, es ohne Entgelt anzubieten.`,
            'Nichts in diesen Bedingungen schließt eine Haftung aus oder beschränkt sie, wo das Gesetz dies nicht erlaubt, etwa bei Vorsatz oder grober Fahrlässigkeit oder bei Tod oder Körperverletzung durch Fahrlässigkeit, noch die Rechte, die das Gesetz Verbrauchern unverzichtbar einräumt.',
          ],
        },
        {
          heading: 'Datenschutz',
          body: [
            'Wir erheben Ihre Dokumente nicht; keine Tracking-Cookies, keine Nutzungsstatistiken. Ihre Einstellungen (Sprache, Design, Einstellungen, die Annahme dieser Bedingungen) bleiben auf Ihrem Gerät, im Speicher des Browsers oder der App.',
            'Wir speichern keine IP-Adressen: Wir zählen nur, wie oft die Seite geöffnet wird, anonym (der Tag und die Art des Browsers, um die Robots der Suchmaschinen zu unterscheiden). Die Desktop-App verbindet sich nur dann mit dem Netz, wenn Sie es verlangen, zum Beispiel mit einem Zeitstempel- oder Sperrprüfungsdienst für eine Signatur.',
          ],
        },
        {
          heading: 'Änderungen',
          body: [
            'Wir können diese Bedingungen aktualisieren. Es gilt die in der App veröffentlichte Fassung mit ihrem Datum; wenn sie sich ändern, bitten wir Sie, sie erneut zu akzeptieren.',
          ],
        },
        {
          heading: 'Anwendbares Recht und Gerichtsstand',
          body: [
            'Diese Bedingungen unterliegen irischem Recht, und für Streitigkeiten sind die Gerichte Irlands zuständig. Als Verbraucher behalten Sie den Schutz der zwingenden Vorschriften des Landes, in dem Sie wohnen, und können dessen Gerichte anrufen. Ist eine Bestimmung unwirksam, bleiben die übrigen in Kraft.',
          ],
        },
      ],
      prevails:
        'Diese Bedingungen sind in mehrere Sprachen übersetzt: Bei Abweichungen ist die englische Fassung maßgeblich.',
      close: 'Schließen',
      acceptBefore: 'Ich habe die',
      acceptLink: 'Nutzungsbedingungen',
      acceptAfter: 'gelesen und akzeptiere sie',
      mustAccept: 'Um eine Datei zu öffnen, akzeptieren Sie zuerst die Nutzungsbedingungen.',
    },
    es: {
      title: 'Condiciones de uso',
      updated: (date: string) => `Versión del ${date}`,
      sections: (who: Provider): Section[] => [
        {
          heading: 'Estas condiciones',
          body: [
            `Regulan el uso de Capriole, la aplicación web en capriole.app y la aplicación de escritorio, ofrecido por ${who.name} (contacto: ${who.contact}). Al usar Capriole las aceptas; si no las aceptas, no lo uses.`,
          ],
        },
        {
          heading: 'Qué es Capriole',
          body: [
            'Un programa gratuito para editar PDF, software libre con licencia 0BSD. Los documentos se procesan en tu dispositivo: no se envían ni a nosotros ni a nadie, y no los vemos ni los guardamos.',
          ],
        },
        {
          heading: 'Sin garantía',
          body: [
            'Capriole se ofrece «tal cual» y «según disponibilidad», sin garantías de ningún tipo, expresas o implícitas, incluidas las de comerciabilidad, idoneidad para un fin concreto y ausencia de errores.',
            'En particular, no garantizamos que un archivo se abra, edite o guarde correctamente; que el resultado sea aceptado por otros programas, organismos o contrapartes; que las firmas digitales, los documentos PDF/A, las censuras y los formularios tengan validez legal o cumplan requisitos legales; ni que el servicio esté siempre disponible.',
          ],
        },
        {
          heading: 'Lo que te corresponde',
          body: [
            'Usas Capriole bajo tu propio riesgo. Antes de editar un documento, guarda una copia. Comprueba el resultado antes de usarlo, enviarlo o firmarlo. Guardar encima de un archivo existente lo sustituye, y el original no se puede recuperar. Tras una censura, comprueba que el contenido ha desaparecido de verdad.',
            'Eres responsable de los documentos que procesas, de tener derecho a modificarlos y del uso que haces de ellos, que debe ser lícito.',
          ],
        },
        {
          heading: 'Limitación de responsabilidad',
          body: [
            `En la máxima medida permitida por la ley, ni ${who.name} ni quienes contribuyen a Capriole responden de daños de ningún tipo, directos o indirectos, derivados del uso de Capriole o de la imposibilidad de usarlo, entre ellos la pérdida, alteración o corrupción de archivos y datos (también al guardar encima de un archivo), documentos rechazados, no válidos o no conformes, lucro cesante, interrupción de la actividad y reclamaciones de terceros. Capriole es gratuito, y esta limitación es una condición esencial para ofrecerlo sin contraprestación.`,
            'Nada en estas condiciones excluye o limita una responsabilidad que la ley no permite excluir o limitar, como la derivada de dolo o culpa grave o de muerte o lesiones personales causadas por negligencia, ni los derechos que la ley reconoce a los consumidores de forma irrenunciable.',
          ],
        },
        {
          heading: 'Privacidad',
          body: [
            'No recogemos tus documentos; ni cookies de seguimiento ni estadísticas de uso. Tus preferencias (idioma, tema, ajustes, la aceptación de estas condiciones) se quedan en tu dispositivo, en la memoria del navegador o de la aplicación.',
            'No registramos direcciones IP: solo contamos cuántas veces se abre la página, de forma anónima (el día y el tipo de navegador, para distinguir los robots de los buscadores). La aplicación de escritorio solo se conecta a la red cuando tú lo pides, por ejemplo a un servicio de sellado de tiempo o de comprobación de revocación para una firma.',
          ],
        },
        {
          heading: 'Cambios',
          body: [
            'Podemos actualizar estas condiciones. Se aplica la versión publicada en la aplicación, con su fecha; cuando cambian, te pedimos que las aceptes de nuevo.',
          ],
        },
        {
          heading: 'Ley aplicable y jurisdicción',
          body: [
            'Estas condiciones se rigen por la ley irlandesa, y los tribunales de Irlanda son competentes para las controversias. Si eres consumidor, conservas la protección de las normas imperativas del país donde resides y puedes acudir a sus tribunales. Si una cláusula resulta inválida, las demás siguen vigentes.',
          ],
        },
      ],
      prevails:
        'Estas condiciones están traducidas a varios idiomas: en caso de diferencias, prevalece la versión inglesa.',
      close: 'Cerrar',
      acceptBefore: 'He leído y acepto las',
      acceptLink: 'condiciones de uso',
      acceptAfter: '',
      mustAccept: 'Para abrir un archivo, acepta primero las condiciones de uso.',
    },
  },
);
