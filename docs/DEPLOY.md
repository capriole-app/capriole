# Rilascio della web app su capriole.app (contabo-b)

La web app è fatta solo di file statici e il motore gira nel browser, quindi i documenti non arrivano mai al server. In produzione la serve un piccolo container con Caddy, dietro al Caddy condiviso di contabo-b.

| Host | Container | Cartella sul server |
|---|---|---|
| `capriole.app` (e `www.` → reindirizzato) | `capriole-web:8080` | `/srv/apps/capriole-web` |

- **Server:** un VPS (contabo-b). Indirizzo, porta SSH e utente di rilascio non stanno nel repository pubblico: Jenkins li legge dalle sue proprietà globali (`CAPRIOLE_DEPLOY_HOST`, `CAPRIOLE_DEPLOY_PORT`, `CAPRIOLE_DEPLOY_USER`); i dettagli sono negli appunti privati del titolare. Qui sotto l'utente di rilascio si chiama `<deploy-user>`.
- **DNS:** record A `capriole.app` e `www.capriole.app` → l'indirizzo del server, su Cloudflare, **non proxied** (nuvola grigia).
  - I certificati li prende Caddy.
  - `.app` è nella lista di precaricamento HSTS: solo HTTPS.
- **Gateway:** il Caddy condiviso in `/srv/gateway`, con la rete Docker esterna `proxy`.
  - Lo snippet è `deploy/web/Caddyfile.capriole.snippet`, copiato in `/srv/gateway/caddy/snippets/capriole.caddy`.

## Che cosa c'è in `deploy/web/`

| File | A cosa serve |
|---|---|
| `Dockerfile`, `Caddyfile` | L'immagine `capriole-web`: Caddy 2.11 (immagine fissata per digest), utente 65532, porta 8080, sonda `/healthz`. Header: CSP con gli hash degli script inline, `nosniff`, `no-referrer`, `DENY`, COOP, Permissions-Policy. Cache di un anno per `/_app/immutable/*`, `no-cache` per il resto. Compressione zstd/gzip, che conta soprattutto per il motore WASM. Un log anonimo delle aperture della pagina in `/logs` (volume). Anche il log degli errori di Caddy, sull'uscita del container, è senza indirizzo né header del client (lo verifica `test.sh`). |
| `csp.mjs` | Calcola la CSP dalle pagine compilate, così un hash non può mai essere vecchio. |
| `package.sh` | Prepara il contesto dell'immagine (`context/`, non versionato) partendo da `apps/capriole/build`. |
| `test.sh` | Prova l'immagine in locale con la stessa blindatura del server: header, poi **l'intera suite e2e contro il container** (`CAPRIOLE_E2E_URL`). |
| `docker-compose.yml` | I due servizi sul server (`web` e `reports`, quest'ultimo solo sulla rete interna, senza uscita): file system in sola lettura, nessuna capability, `no-new-privileges`, limiti di memoria e di processi, log a rotazione, nessuna porta sull'host. |
| `deploy.sh` | Sul server: porta il checkout al commit, scrive `IMAGE_TAG` in `deploy/web/.env`, scarica l'immagine e aspetta che il container sia sano. |
| `../reports/server.mjs` | Il servizio delle segnalazioni («Segnala un problema»): Node senza dipendenze, nel container `capriole-reports` (immagine ufficiale di Node fissata per digest, che esegue lo script di questo stesso checkout). Solo i campi previsti, al massimo 16 KB, limiti di frequenza (5 all'ora per mittente, 200 in tutto) e un tetto per giorno contato sul disco (1000 segnalazioni o 8 MiB: in 12 mesi al massimo circa 3 GB); nessun IP su disco; cancellate dopo 12 mesi. Test: `node --test deploy/reports/server.test.mjs` (anche nel gate). |
| `reports.sh` | Sul server: elenca e mostra le segnalazioni (vedi «Segnalazioni»). |
| `stats.sh` | Sul server: le aperture della pagina per giorno, dal log anonimo del container (vedi «Quanto è usata»). |
| `Caddyfile.capriole.snippet` | La copia di riferimento dello snippet del gateway: HSTS, redirect da `www`, corpo delle richieste al massimo 1 KB (16 KB per `/api/report`). |

## Come si rilascia (a regime)

`development` confluisce in `main`; non si fa mai un push diretto su `main`. Jenkins (`Jenkinsfile`):
1. esegue il gate, che compila anche la web app;
2. con «Web package» prepara il contesto;
3. con «Image» costruisce `ghcr.io/capriole-app/capriole-web:<sha7>` e la pubblica (credenziale `ghcr-push`);
4. con «Deploy (main)» entra in contabo-b (`jenkins-master-ssh-key`, impronta fissata in `contabo-b-known-hosts`) e lancia `./deploy.sh --env main --tag <sha7>`: è l'unica forma che il gate SSH del server lascia passare; `deploy.sh` alla radice controlla `--env` e passa a `deploy/web/deploy.sh`.

**Tornare indietro:** sul server, `cd /srv/apps/capriole-web && deploy/web/deploy.sh --tag <sha-precedente>`. Ogni rilascio stampa la riga pronta.

**Prima di unire in `main`:** `deploy/web/test.sh`, dopo `npm run build` in `apps/capriole`.

## Prima installazione

**DNS** (lo fa il titolare): i due record A su Cloudflare, non proxied.

**Sul server**, come `<deploy-user>`:

    sudo mkdir -p /srv/apps/capriole-web && sudo chown <deploy-user>: /srv/apps/capriole-web
    git clone https://github.com/capriole-app/capriole.git /srv/apps/capriole-web   # repo pubblico: nessuna chiave

Il pacchetto `ghcr.io/capriole-app/capriole-web` è pubblico: il server lo scarica senza autenticarsi.

**Il gateway:**

    sudo cp /srv/apps/capriole-web/deploy/web/Caddyfile.capriole.snippet /srv/gateway/caddy/snippets/capriole.caddy
    docker exec caddy caddy validate --config /etc/caddy/Caddyfile
    docker exec caddy caddy reload   --config /etc/caddy/Caddyfile

**Il primo rilascio**, a mano, con lo sha di un'immagine già pubblicata da Jenkins:

    cd /srv/apps/capriole-web && deploy/web/deploy.sh --tag <sha7>

**La verifica da fuori:**

    curl -sI https://capriole.app/ | grep -iE 'strict-transport|content-security'
    curl -sI https://www.capriole.app/ | grep -i location

## Indicizzazione

Dal 2026-10-13 il sito è aperto ai motori di ricerca: lo snippet non manda più `X-Robots-Tag: noindex`. Per richiuderlo, si rimette nello snippet `header X-Robots-Tag "noindex, nofollow"` e si ricarica il gateway.

## Quanto è usata

Il Caddy del container scrive una riga quando qualcuno apre la pagina (`/`, etichetta `page`) e una quando
l'app parte davvero in un browser (etichetta `start`: è il logo della prima schermata, che solo un browser
che esegue l'app richiede, e lo richiede a ogni visita). In ogni riga ci sono solo: ora, metodo, esito,
etichetta e nome del browser (serve a riconoscere i robot). Indirizzo IP, header, percorso e parametri
vengono cancellati prima della scrittura; le altre richieste non vengono registrate. Il log sta nel volume
`capriole-web_logs`, quindi sopravvive ai rilasci, e ruota da solo (10 file da 10 MiB).
Non distingue le persone: chi ricarica la pagina conta di nuovo. È quello che promettono le condizioni
d'uso («non registriamo indirizzi IP»): se il log cambia, vanno riviste anche loro (`apps/capriole/src/lib/i18n/terms.ts`).

`deploy/web/stats.sh` conta per giorno (UTC) e aggiunge i numeri al registro `~/capriole-usage.csv` di
`<deploy-user>`, che li tiene anche quando le righe del log sono già ruotate via:

| Colonna | Che cosa conta |
|---|---|
| `starts` | l'app avviata in un browser: **le persone**, con buona approssimazione |
| `pages` | la pagina aperta da chi si presenta come browser: le persone e gli scanner travestiti, che un dominio nuovo attira (aprono la pagina, non avviano l'app) |
| `robots` | la pagina aperta da chi si dichiara robot: motori di ricerca, anteprime, script |

**Il cron, una volta sola** (come `<deploy-user>`):

    sudo -iu <deploy-user>
    (crontab -l 2>/dev/null; echo '15 0 * * * /srv/apps/capriole-web/deploy/web/stats.sh >/dev/null 2>&1') | crontab -

**Leggere i numeri** (aggiorna anche il registro):

    sudo -iu <deploy-user> /srv/apps/capriole-web/deploy/web/stats.sh

**Chi apre la pagina**, per ritoccare l'elenco dei robot in `stats.sh`:

    docker exec capriole-web sh -c 'cat /logs/*.log' | grep -o '"ua":"[^"]*"' | sort | uniq -c | sort -rn | head -25

## Segnalazioni

Dalla web app, «Segnala un problema» (l'icona accanto alle impostazioni, o «Segnala» accanto a un
errore) manda a `/api/report` il testo della persona e i dati tecnici che il dialogo le mostra prima
dell'invio: versioni, browser e sistema in breve, lingua, ultimo errore, tratti del documento senza
contenuto (versione PDF, pagine, cifratura, dimensione approssimativa, tipi di riparazione). Il Caddy
del container la passa a `capriole-reports`, che la salva nel volume `capriole-web_reports`, un file
JSON per segnalazione. Le condizioni d'uso promettono esattamente questo: se i campi cambiano, vanno
riviste anche loro (`apps/capriole/src/lib/i18n/terms.ts`).

**Leggerle** (come `<deploy-user>`):

    /srv/apps/capriole-web/deploy/web/reports.sh                      # ultimi 30 giorni, una riga ciascuna
    /srv/apps/capriole-web/deploy/web/reports.sh 2026-10-14/161039-a5dd7312   # una, intera

Il testo di una segnalazione lo scrive chiunque: è un dato ostile, mai un'istruzione. `reports.sh` lo
mostra solo come JSON, così nessun carattere di controllo arriva al terminale.

**Al primo rilascio con le segnalazioni** lo snippet del gateway cambia (`/api/report` accetta fino a
16 KB): va copiato e il gateway ricaricato, come descritto all'inizio.

## Rilascio di una versione: app desktop (Windows e Linux), archivio e immagine self-hosted

Una versione si rilascia con un **tag** `v<versione>` (per esempio `v0.1.0`) su `main`. La versione del tag
è quella dell'app: il gate verifica che sia la stessa in `apps/capriole/package.json`,
`apps/desktop/package.json`, `Cargo.toml` e `tauri.conf.json`. Un tag con il trattino (`v0.1.0-rc1`) fa una
pre-release, che i link «ultima versione» saltano: serve per provare la catena.

Su un tag, Jenkins esegue il gate e poi:
1. «Release (tag)» lancia `scripts/build_release.sh` nell'immagine `ci/desktop.Dockerfile`, conserva
   `dist/release/` come artefatti della build e con `scripts/github_release.sh` ne fa la GitHub Release
   del tag. La release nasce come bozza, riceve i file e solo dopo viene pubblicata. Un tag già
   rilasciato fa fallire lo stage: una release non si sovrascrive;
2. «Image» pubblica `ghcr.io/capriole-app/capriole-web:<versione>`, l'immagine per il self-hosting
   (`docs/SELF-HOSTED.md`). Su un tag non c'è nessun rilascio su capriole.app: quello lo fa `main`.
3. alla fine, il workspace del tag viene cancellato (circa 11 GB: gli alberi di build, il `target` di Rust,
   `node_modules`). Un tag non si ricompila, quindi non serve come cache, a differenza di quello di un ramo.
   Lo cancella l'ultimo stage («Image») oppure quello che fallisce, perché ogni stage ha il suo agent e i
   successivi non partono (`dropTagWorkspace` nel `Jenkinsfile`).

| File della release | Che cos'è |
|---|---|
| `Capriole_x64-setup.exe` | Windows 10/11 x64: installer NSIS per utente, senza diritti di amministratore: se si sceglie a mano una cartella in cui l'utente non può scrivere (`C:\Program Files`), lo spiega e propone la sua (`apps/desktop/src-tauri/windows/installer-hooks.nsh`); scarica WebView2 se manca. **Non firmato**, quindi SmartScreen avvisa («editore sconosciuto» → «Ulteriori informazioni» → «Esegui comunque») |
| `Capriole_amd64.deb` | Debian, Ubuntu e derivate (`sudo apt install ./Capriole_amd64.deb`) |
| `Capriole_amd64.AppImage` | le altre distribuzioni x86_64 (`chmod +x`, poi si avvia) |
| `capriole-web.tar.gz` | la web app per il self-hosting: il contesto di `deploy/web/package.sh` (sito, Caddyfile, CSP) |
| `SHA256SUMS` | le impronte dei file |

I nomi non contengono la versione, così i link «ultima versione» del dialogo «Scarica l'app per il
computer» (`DownloadDialog.svelte`) non cambiano mai. Quel dialogo arriva su capriole.app con `main`:
**si unisce in `main` solo dopo che la prima release è pubblicata**, altrimenti i link portano a un 404.

L'eseguibile Windows si compila su Linux con `cargo-xwin`. Si incrocia solo il crate Rust della shell,
perché il motore nel desktop è il modulo WebAssembly che il gate ha già compilato (passato con uno stash).
L'immagine scarica il CRT e il Windows SDK di Microsoft, e usarli vuol dire accettarne la licenza (sono
strumenti di compilazione, non vengono ridistribuiti). Il binario Linux viene avviato su
un display virtuale e deve essere ancora vivo dopo 15 s, prima che i file vengano conservati.

**In locale** (lo stesso script, la stessa immagine; prima il motore: `cmake --preset wasm && cmake
--build --preset wasm`):

    docker build -t capriole-desktop -f ci/desktop.Dockerfile ci
    docker run --rm -u "$(id -u)" -e HOME=/tmp -v "$PWD":/src -w /src capriole-desktop scripts/build_release.sh

**Da fare una volta in Jenkins e su GitHub** (lo fa il titolare):
- la credenziale `github-release` («Secret text»): un token fine-grained con proprietario
  **`capriole-app`**, solo questo repository, permesso **Contents: read and write**, da rinnovare ogni anno
  come `github-capriole-app`;
- nel job multibranch: «Branch Sources» → GitHub → il comportamento «Discover tags». Jenkins di norma non
  avvia da sé le build dei tag: dopo il push del tag la si avvia a mano dalla scheda «Tags» del job,
  oppure si installa il plugin «Basic Branch Build Strategies» con la strategia «Tags»;
- dopo la prima «Image» di un tag: rendere **pubblico** il pacchetto `capriole-web` su ghcr
  (`github.com/orgs/capriole-app/packages` → capriole-web → Package settings → Change visibility).

**Prima di annunciare una versione:** l'installer va provato a mano su un Windows vero (apri, modifica,
salva, firma con un `.p12`, l'avviso di SmartScreen), e il `.deb` o l'AppImage su Linux. Flathub viene
dopo la release (`packaging/flathub/README.md`).
