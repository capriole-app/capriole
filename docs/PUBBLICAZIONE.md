# Pubblicazione

Cosa è pronto e cosa resta da fare quando si decide di rendere Capriole pubblico. **Niente di questo
avviene da solo**: ogni passo esterno lo fa (o lo autorizza) il titolare del progetto.

## Già pronto nel repository

| Cosa | Dove |
|---|---|
| Licenza 0BSD e motivazione | `LICENSE`, `docs/adr/0009-licenza.md` |
| Avvisi delle componenti nel motore (testi esatti, generati) | `THIRD_PARTY_NOTICES.md`, `third_party/notices.json` (`scripts/gen_notices.py`) |
| Licenze nelle app: «Informazioni e licenze» (motore, pacchetti JS nel bundle, risorse, crate Rust sul desktop) | `apps/capriole` (dialogo), `apps/inspector` (`licenses.txt`); plugin `packages/engine-client/scripts/licenses.mjs`; crate: `scripts/gen_crate_notices.py` |
| File da progetto pubblico | `README.md`, `CONTRIBUTING.md` (DCO), `SECURITY.md`, `CODE_OF_CONDUCT.md` (Contributor Covenant 2.1), `.github/` (modelli di issue e PR, nessun workflow) |
| Chiavi di test dichiarate come tali | `tests/data/signing/README.md` |
| Kit OSS-Fuzz | `ci/oss-fuzz/` (`project.yaml`, `Dockerfile`, `build.sh`, `fetch_deps.py`; `emulate.sh` per provarlo in locale) |
| Verifica pre-pubblicazione | `scripts/prepublish_check.sh` |

## Decisioni prese (2026-10-14)

| Decisione | Dove |
|---|---|
| Il repository sta nell'organizzazione GitHub **`capriole-app`**: `github.com/capriole-app/capriole` (`capriole` era già preso) | `.github/`, `ci/oss-fuzz/`, `CLAUDE.md`, «Informazioni e licenze» nell'app |
| Le immagini della web app: `ghcr.io/capriole-app/capriole-web`, pacchetto pubblico | `Jenkinsfile`, `deploy/web/docker-compose.yml` |
| Contatti, tutti `@capriole.app` (inoltro di Cloudflare Email Routing): `security@` per vulnerabilità, condotta e OSS-Fuzz; `hello@` per tutto il resto e per le condizioni d'uso | `SECURITY.md`, `CODE_OF_CONDUCT.md`, `ci/oss-fuzz/project.yaml`, `README.md`, `apps/capriole/src/lib/terms-version.ts` |
| Nessun indirizzo personale o aziendale, nemmeno nella storia: autore dei commit `Nicola <19620133+nik71b14@users.noreply.github.com>`; la storia già scritta si riscrive (sotto) | metadati git; `scripts/prepublish_check.sh` lo verifica |
| Indirizzo, porta SSH e utente del server fuori dal repository: proprietà globali di Jenkins `CAPRIOLE_DEPLOY_HOST`, `CAPRIOLE_DEPLOY_PORT`, `CAPRIOLE_DEPLOY_USER` | `Jenkinsfile`, `docs/DEPLOY.md` |

| Ancora da confermare | Dove |
|---|---|
| Crate MPL-2.0 portati da Tauri nell'app desktop (`cssparser`, `selectors`, `dtoa-short`, `option-ext`): copyleft a livello di file, usati senza modifiche, sorgente indicato nella pagina delle licenze | `docs/DEPENDENCIES.md` (la regola «nessun copyleft linkato» nasceva per il motore) |
| OSS-Fuzz chiede che `primary_contact` sia legato a un account Google: creare un account Google con `security@capriole.app` (si può, con un indirizzo esistente) | `ci/oss-fuzz/project.yaml` |

`scripts/prepublish_check.sh` fallisce finché resta un segnaposto o un indirizzo e-mail che non sia del
progetto (`@capriole.app`), di GitHub (noreply) o un esempio — nei file di ogni commit e nei metadati.

## Il giorno della pubblicazione (in quest'ordine)

Fatto il 2026-10-14: il repository è pubblico su `github.com/capriole-app/capriole`. I passi restano come
riferimento (rinnovo del token, OSS-Fuzz).

**Prima, fuori dal repository (il titolare):**
1. Cloudflare → `capriole.app` → Email Routing: `security@` e `hello@` inoltrati alla casella del titolare
   (record MX e SPF li aggiunge Cloudflare; non toccano i record A del sito).
2. GitHub → Settings → Emails: **Keep my email addresses private** e **Block command line pushes that
   expose my email**. Creare l'organizzazione gratuita `capriole-app`.
3. Jenkins → Manage Jenkins → System → Global properties: `CAPRIOLE_DEPLOY_HOST`, `CAPRIOLE_DEPLOY_PORT`,
   `CAPRIOLE_DEPLOY_USER` (i valori negli appunti privati, `DOCS/DEPLOY-server-privato.md`).

**La storia riscritta** (su un clone: il repository di lavoro resta com'è finché tutto non è verificato):
4. In `apps/capriole/src/lib/terms-version.ts`, `termsVersion` = la data del giorno (il contatto delle
   condizioni è cambiato: si chiede di nuovo di accettarle). Merge in `development`, gate verde.
5. Clone fresco, solo `main` e `development`; `git filter-repo` con il mailmap (autore → noreply) e con
   `--replace-text` per gli indirizzi e i dettagli del server rimasti nei file dei vecchi commit.
   Poi `scripts/prepublish_check.sh` **senza** `--allow-pending`: deve dire «Ready to publish».
6. Il vecchio `nik71b14/capriole` va **cancellato** (non trasferito): GitHub tiene raggiungibili per hash
   i commit di un repository anche dopo un force push, e con loro i vecchi indirizzi. Poi push del clone
   riscritto nel nuovo `capriole-app/capriole`, vuoto, **pubblico**.
7. Il repository di lavoro locale si sostituisce con il clone riscritto; `git config user.email` = l'indirizzo
   noreply; `git config core.hooksPath scripts/hooks`.

**Dopo il push (cosa cambia per la CI e il server, non si rifà nulla da zero):**
8. Jenkins: nel job, l'URL del repository diventa `https://github.com/capriole-app/capriole.git`, con la
   credenziale `github-capriole-app`: un token fine-grained con proprietario **`capriole-app`** (Contents e
   Metadata in lettura, Commit statuses in scrittura). L'organizzazione rifiuta i token fine-grained che
   durano più di 366 giorni: **va rinnovato ogni anno** (il primo scade a ottobre 2027). Webhook, se c'era,
   sul nuovo repository. Gli hash dei commit sono
   cambiati: la prima build di ogni ramo riparte da zero.
9. ghcr: il token della credenziale `ghcr-push` deve poter scrivere nell'organizzazione (`write:packages`,
   SSO dell'organizzazione se richiesto). Dopo la prima immagine: pacchetto `capriole-web` → visibilità
   **Public**, così il server la scarica senza autenticarsi.
10. Sul server, in `/srv/apps/capriole-web`: `git remote set-url origin https://github.com/capriole-app/capriole.git`
    e `git fetch origin` (la deploy key del vecchio repository non serve più: si può togliere).
    Il primo rilascio da Jenkins verifica tutto.

**Le impostazioni del nuovo repository:**
11. Settings → Code security: **Private vulnerability reporting**, Dependabot alerts.
12. Settings → Actions → **Disable actions** (la CI è il Jenkins del workspace: niente minuti a pagamento).
13. Settings → Branches: proteggere `main` (niente push diretti, come già l'hook locale).
14. Facoltativo: tag `v0.1.0` su `main` e una release con le note.
15. OSS-Fuzz: fork di `google/oss-fuzz`, copiare `ci/oss-fuzz/{project.yaml,Dockerfile,build.sh}` in
    `projects/capriole/`, provarlo con `python3 infra/helper.py build_fuzzers capriole` e
    `check_build capriole` (serve Docker), aprire la PR. Il progetto deve essere pubblico e la PR la
    apre il titolare (va accettata da Google).

## Prima prova del kit OSS-Fuzz senza Docker

```sh
ci/oss-fuzz/fetch_deps.py /tmp/capriole-deps        # gli archivi delle dipendenze (rete), SHA-256 verificati
CAPRIOLE_JOBS=4 nice ci/oss-fuzz/emulate.sh /tmp/capriole-deps address 20
CAPRIOLE_JOBS=4 nice ci/oss-fuzz/emulate.sh /tmp/capriole-deps undefined 20
```

`emulate.sh` esegue `build.sh` con i flag di OSS-Fuzz (libFuzzer + il sanitizer scelto) senza rete, poi
ogni target per i secondi indicati sul suo corpus seme.
