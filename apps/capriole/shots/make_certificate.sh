#!/usr/bin/env bash
# The certificate of the signature scene (shots.spec.ts): a throwaway CA and a signer whose names read as
# a real person's would, into <directory>/anna-bianchi.p12 (password "capriole"). Made at each run and never
# committed; the signing tests keep their own fixtures (tests/data/signing/), whose names say "(test)".
# Invented names: "Example Trust CA" is no real authority.
set -euo pipefail
dir="${1:?usage: make_certificate.sh <directory>}"
mkdir -p "$dir"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

openssl ecparam -name prime256v1 -out "$work/p256.pem"
openssl req -x509 -newkey "ec:$work/p256.pem" -nodes -keyout "$work/ca.key" -out "$work/ca.pem" -days 3650 \
  -subj "/C=IE/O=Example Trust/CN=Example Trust CA" \
  -addext "basicConstraints=critical,CA:TRUE" -addext "keyUsage=critical,keyCertSign,cRLSign" -set_serial 1 2>/dev/null
openssl req -new -newkey "ec:$work/p256.pem" -nodes -keyout "$work/signer.key" -out "$work/signer.csr" \
  -subj "/C=IE/O=Aurora Studio Ltd/CN=Anna Bianchi" 2>/dev/null
printf '%s\n' 'basicConstraints=critical,CA:FALSE' 'keyUsage=critical,digitalSignature,nonRepudiation' > "$work/signer.ext"
openssl x509 -req -in "$work/signer.csr" -CA "$work/ca.pem" -CAkey "$work/ca.key" -set_serial 2 -days 1095 \
  -out "$work/signer.pem" -extfile "$work/signer.ext" 2>/dev/null
openssl pkcs12 -export -inkey "$work/signer.key" -in "$work/signer.pem" -certfile "$work/ca.pem" -name "Anna Bianchi" \
  -passout pass:capriole -out "$dir/anna-bianchi.p12"
