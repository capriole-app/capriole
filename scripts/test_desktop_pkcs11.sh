#!/usr/bin/env bash
# The desktop shell's PKCS#11 tests (apps/desktop/src-tauri/src/pkcs11.rs, PLAN.md phase 11c) against a
# software token: SoftHSM2 (softhsm2-util, libsofthsm2.so) and OpenSC's pkcs11-tool, external test tools,
# never linked. A token made for the run, in a temporary directory, with the RSA key and certificate of
# tests/data/signing/rsa.p12 as id 01, PIN 1234.
#   scripts/test_desktop_pkcs11.sh [module]   (default: /usr/lib/softhsm/libsofthsm2.so)
set -euo pipefail
cd "$(dirname "$0")/.."
module=${1:-/usr/lib/softhsm/libsofthsm2.so}
[[ -f "$module" ]] || { echo "no PKCS#11 module at $module (apt install softhsm2)" >&2; exit 1; }
command -v pkcs11-tool >/dev/null || { echo "pkcs11-tool not found (apt install opensc)" >&2; exit 1; }
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
mkdir "$work/tokens"
printf 'directories.tokendir = %s\nobjectstore.backend = file\n' "$work/tokens" > "$work/softhsm2.conf"
export SOFTHSM2_CONF="$work/softhsm2.conf"
softhsm2-util --init-token --free --label capriole --pin 1234 --so-pin 123456 >/dev/null
p12=tests/data/signing/rsa.p12
openssl pkcs12 -in "$p12" -passin pass:capriole -nocerts -nodes 2>/dev/null | openssl pkey -outform DER -out "$work/key.der"
openssl pkcs12 -in "$p12" -passin pass:capriole -nokeys -clcerts 2>/dev/null | openssl x509 -outform DER -out "$work/cert.der"
pkcs11-tool --module "$module" --login --pin 1234 --write-object "$work/key.der" --type privkey --id 01 --label rsa >/dev/null
pkcs11-tool --module "$module" --login --pin 1234 --write-object "$work/cert.der" --type cert --id 01 --label rsa >/dev/null
rm "$work/key.der"
export PATH="$HOME/.cargo/bin:$PATH"
cd apps/desktop/src-tauri
CAPRIOLE_PKCS11_MODULE="$module" cargo test --locked -- --test-threads=1 --nocapture pkcs11
