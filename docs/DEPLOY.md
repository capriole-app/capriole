# Rilascio della web app su capriole.app (contabo-b)

La web app è fatta solo di file statici e il motore gira nel browser, quindi i documenti non arrivano mai al server. In produzione la serve un piccolo container con Caddy, dietro al Caddy condiviso di contabo-b.

| Host | Container | Cartella sul server |
|---|---|---|
| `capriole.app` (e `www.` → reindirizzato) | `capriole-web:8080` | `/srv/apps/capriole-web` |

- **Server:** contabo-b, indirizzo, porta SSH e utente negli appunti privati del titolare.
- **DNS:** record A `capriole.app` e `www.capriole.app` → l'indirizzo del server, su Cloudflare, **non proxied** (nuvola grigia).
  - I certificati li prende Caddy.
  - `.app` è nella lista di precaricamento HSTS: solo HTTPS.
- **Gateway:** il Caddy condiviso in `/srv/gateway`, con la rete Docker esterna `proxy`.
  - Lo snippet è `deploy/web/Caddyfile.capriole.snippet`, copiato in `/srv/gateway/caddy/snippets/capriole.caddy`.

## Che cosa c'è in `deploy/web/`

| File | A cosa serve |
|---|---|
| `Dockerfile`, `Caddyfile` | L'immagine `capriole-web`: Caddy 2.11 (immagine fissata per digest), utente 65532, porta 8080, sonda `/healthz`. Header: CSP con gli hash degli script inline, `nosniff`, `no-referrer`, `DENY`, COOP, Permissions-Policy. Cache di un anno per `/_app/immutable/*`, `no-cache` per il resto. Compressione zstd/gzip, che conta soprattutto per il motore WASM. Un log anonimo delle aperture della pagina in `/logs` (volume). |
| `csp.mjs` | Calcola la CSP dalle pagine compilate, così un hash non può mai essere vecchio. |
| `package.sh` | Prepara il contesto dell'immagine (`context/`, non versionato) partendo da `apps/capriole/build`. |
| `test.sh` | Prova l'immagine in locale con la stessa blindatura del server: header, poi **l'intera suite e2e contro il container** (`CAPRIOLE_E2E_URL`). |
| `docker-compose.yml` | Il servizio sul server: file system in sola lettura, nessuna capability, `no-new-privileges`, limiti di memoria e di processi, log a rotazione, nessuna porta sull'host. |
| `deploy.sh` | Sul server: porta il checkout al commit, scrive `IMAGE_TAG` in `deploy/web/.env`, scarica l'immagine e aspetta che il container sia sano. |
| `stats.sh` | Sul server: le aperture della pagina per giorno, dal log anonimo del container (vedi «Quanto è usata»). |
| `Caddyfile.capriole.snippet` | La copia di riferimento dello snippet del gateway: HSTS, redirect da `www`. |

## Come si rilascia (a regime)

`development` confluisce in `main`; non si fa mai un push diretto su `main`. Jenkins (`Jenkinsfile`):
1. esegue il gate, che compila anche la web app;
2. con «Web package (main)» prepara il contesto;
3. con «Image (main)» costruisce `ghcr.io/capriole-app/capriole-web:<sha7>` e la pubblica (credenziale `ghcr-push`);
4. con «Deploy (main)» entra in contabo-b (`jenkins-master-ssh-key`, impronta fissata in `contabo-b-known-hosts`) e lancia `./deploy.sh --env main --tag <sha7>`: è l'unica forma che il gate SSH del server lascia passare; `deploy.sh` alla radice controlla `--env` e passa a `deploy/web/deploy.sh`.

**Tornare indietro:** sul server, `cd /srv/apps/capriole-web && deploy/web/deploy.sh --tag <sha-precedente>`. Ogni rilascio stampa la riga pronta.

**Prima di unire in `main`:** `deploy/web/test.sh`, dopo `npm run build` in `apps/capriole`.

## Prima installazione

**DNS** (lo fa il titolare): i due record A su Cloudflare, non proxied.

**Sul server**, come `<deploy-user>`:

    sudo mkdir -p /srv/apps/capriole-web && sudo chown <deploy-user>: /srv/apps/capriole-web
    ssh-keygen -t ed25519 -N '' -f ~/.ssh/capriole-deploy -C 'contabo-b capriole'
    # la .pub su GitHub: repo capriole → Settings → Deploy keys (sola lettura)
    git clone -c core.sshCommand='ssh -i ~/.ssh/capriole-deploy -o IdentitiesOnly=yes' \
      git@github.com:nik71b14/capriole.git /srv/apps/capriole-web

Il server deve poter scaricare da `ghcr.io/capriole-app/`. Se il pacchetto `capriole-web` ha una visibilità diversa, va dato accesso al token usato dal server.

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

Il Caddy del container scrive una riga per ogni apertura della pagina (`/`), e in quella riga c'è solo:
ora, metodo, esito e nome del browser (per riconoscere i robot). Indirizzo IP, header e percorso vengono
cancellati prima della scrittura; le richieste dei file dell'app non vengono registrate. Il log sta nel
volume `capriole-web_logs`, quindi sopravvive ai rilasci, e ruota da solo (10 file da 10 MiB).
Non distingue le persone: chi ricarica la pagina conta di nuovo. È quello che promettono le condizioni
d'uso («non registriamo indirizzi IP»): se il log cambia, vanno riviste anche loro (`apps/capriole/src/lib/i18n/terms.ts`).

`deploy/web/stats.sh` conta le aperture per giorno (UTC), separando i robot, e le aggiunge al registro
`~/capriole-visits.csv` di `<deploy-user>` (`giorno,visite,robot`). Il registro tiene i totali anche quando le
righe del log sono già ruotate via.

**Il cron, una volta sola** (come `<deploy-user>`):

    sudo -iu <deploy-user>
    (crontab -l 2>/dev/null; echo '15 0 * * * /srv/apps/capriole-web/deploy/web/stats.sh >/dev/null 2>&1') | crontab -

**Leggere i numeri** (aggiorna anche il registro):

    sudo -iu <deploy-user> /srv/apps/capriole-web/deploy/web/stats.sh
