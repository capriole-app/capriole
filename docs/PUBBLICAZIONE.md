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

## Decisioni ancora aperte

| Decisione | Dove va | Stato |
|---|---|---|
| Indirizzo di contatto per la sicurezza e la condotta | `SECURITY.md`, `CODE_OF_CONDUCT.md` (segnaposto `<<CONTACT-TBD>>`) | da decidere |
| Contatto principale OSS-Fuzz (indirizzo legato a un account Google: riceve i crash) | `ci/oss-fuzz/project.yaml` (`<<CONTACT-TBD>>`) | da decidere |
| L'indirizzo e-mail nei commit (`19620133+nik71b14@users.noreply.github.com`) diventa pubblico con la storia | metadati git | da confermare (per i commit futuri si può usare l'indirizzo noreply di GitHub) |
| Crate MPL-2.0 portati da Tauri nell'app desktop (`cssparser`, `selectors`, `dtoa-short`, `option-ext`): copyleft a livello di file, usati senza modifiche, sorgente indicato nella pagina delle licenze | `docs/DEPENDENCIES.md` | da confermare (la regola «nessun copyleft linkato» nasceva per il motore) |

`scripts/prepublish_check.sh` fallisce finché resta un segnaposto; con `--allow-pending` li elenca soltanto.

## Il giorno della pubblicazione (in quest'ordine)

1. Compilare i segnaposto (`git grep '<<CONTACT-TBD>>'`), poi `scripts/prepublish_check.sh` **senza**
   `--allow-pending`: deve dire «Ready to publish». Gate verde.
2. Push di `development`, merge in `main` (l'hook esegue il gate).
3. GitHub → Settings → General → **Change visibility → Public**.
4. Settings → Code security: attivare **Private vulnerability reporting**, Dependabot alerts.
5. Settings → Actions → **Disable actions** (la CI è il Jenkins del workspace: niente minuti a pagamento).
6. Settings → Branches: proteggere `main` (niente push diretti, come già l'hook locale).
7. Facoltativo: tag `v0.1.0` su `main` e una release con le note.
8. OSS-Fuzz: fork di `google/oss-fuzz`, copiare `ci/oss-fuzz/{project.yaml,Dockerfile,build.sh}` in
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
