#!/usr/bin/env bash
# The revocation fixtures (PLAN.md phase 11b), with OpenSSL, under the test CA of make_fixtures.sh (which
# runs this at its end; alone, it keeps ca.key / ca.pem). Throwaway keys, not secrets:
#   - ocsp.key / ocsp.pem      "Capriole Test OCSP", a responder the CA authorised (id-kp-OCSPSigning, no-check);
#   - ltv.p12                  "Luca Verdi (test)", RSA 2048, password "capriole": a signer whose certificate says
#                              where to ask (OCSP, the CA's certificate, the CRL — at http://127.0.0.1:9/, where
#                              nothing answers: whatever the checks find, they find offline);
#   - revoked.p12              "Rita Neri (test)", the same, revoked on 2026-01-01;
#   - ltv-tsa.key / ltv-tsa.pem a time-stamping authority that says where to ask too;
#   - ca-index.txt             the CA's database (openssl ca and openssl ocsp -index);
#   - ca.crl                   the CA's CRL (DER), listing revoked.p12's certificate;
#   - ocsp-ltv.der             the responder's OCSP answer for ltv.p12's certificate: good;
#   - ocsp-revoked.der         the CA's own answer for revoked.p12's: revoked.
set -euo pipefail
cd "$(dirname "$0")"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

days=36500
where=$'authorityInfoAccess=OCSP;URI:http://127.0.0.1:9/ocsp,caIssuers;URI:http://127.0.0.1:9/ca.cer\ncrlDistributionPoints=URI:http://127.0.0.1:9/ca.crl'
issue() {  # name, subject, serial, extensions
  openssl req -new -newkey rsa:2048 -nodes -keyout "$work/$1.key" -out "$work/$1.csr" -subj "$2" 2>/dev/null
  printf '%s\n' "$4" > "$work/$1.ext"
  openssl x509 -req -in "$work/$1.csr" -CA ca.pem -CAkey ca.key -set_serial "$3" -days $days -out "$work/$1.pem" -extfile "$work/$1.ext" 2>/dev/null
}
issue ocsp "/C=IT/O=Capriole/CN=Capriole Test OCSP" 9 $'basicConstraints=critical,CA:FALSE\nkeyUsage=critical,digitalSignature\nextendedKeyUsage=critical,OCSPSigning\nnoCheck=ignored'
issue ltv "/C=IT/O=Capriole/CN=Luca Verdi (test)" 10 $'basicConstraints=critical,CA:FALSE\nkeyUsage=critical,digitalSignature,nonRepudiation\n'"$where"
issue revoked "/C=IT/O=Capriole/CN=Rita Neri (test)" 11 $'basicConstraints=critical,CA:FALSE\nkeyUsage=critical,digitalSignature,nonRepudiation\n'"$where"
issue ltv-tsa "/C=IT/O=Capriole/CN=Capriole Test TSA (LTV)" 12 $'basicConstraints=critical,CA:FALSE\nkeyUsage=critical,digitalSignature\nextendedKeyUsage=critical,timeStamping\n'"$where"
cp "$work/ocsp.key" ocsp.key
cp "$work/ocsp.pem" ocsp.pem
cp "$work/ltv-tsa.key" ltv-tsa.key
cp "$work/ltv-tsa.pem" ltv-tsa.pem
openssl pkcs12 -export -inkey "$work/ltv.key" -in "$work/ltv.pem" -certfile ca.pem -name "Luca Verdi" -passout pass:capriole -out ltv.p12
openssl pkcs12 -export -inkey "$work/revoked.key" -in "$work/revoked.pem" -certfile ca.pem -name "Rita Neri" -passout pass:capriole -out revoked.p12

# The CA's database: every certificate it issued that has a place to ask, valid, but revoked.p12's.
row() {  # status, certificate, revocation time
  local expiry serial subject
  expiry=$(date -u -d "$(openssl x509 -in "$2" -noout -enddate | cut -d= -f2)" +%Y%m%d%H%M%SZ)
  serial=$(openssl x509 -in "$2" -noout -serial | cut -d= -f2)
  subject=$(openssl x509 -in "$2" -noout -subject -nameopt compat | sed 's/^subject=//')
  printf '%s\t%s\t%s\t%s\tunknown\t%s\n' "$1" "$expiry" "$3" "$serial" "$subject"
}
{
  row V ocsp.pem ""
  row V "$work/ltv.pem" ""
  row R "$work/revoked.pem" "260101000000Z"
  row V ltv-tsa.pem ""
} > ca-index.txt

cat > "$work/ca.cnf" <<CNF
[ ca ]
default_ca = test
[ test ]
database = $work/index.txt
crlnumber = $work/crlnumber
default_md = sha256
default_crl_days = $days
CNF
cp ca-index.txt "$work/index.txt"
echo 01 > "$work/crlnumber"
openssl ca -config "$work/ca.cnf" -gencrl -keyfile ca.key -cert ca.pem -out "$work/ca.crl.pem" 2>/dev/null
openssl crl -in "$work/ca.crl.pem" -outform DER -out ca.crl

answer() {  # certificate, out, signer certificate, signer key
  openssl ocsp -issuer ca.pem -cert "$1" -no_nonce -reqout "$work/request.der" 2>/dev/null
  openssl ocsp -index ca-index.txt -CA ca.pem -rsigner "$3" -rkey "$4" -reqin "$work/request.der" -respout "$2" -ndays $days 2>/dev/null
}
answer "$work/ltv.pem" ocsp-ltv.der ocsp.pem ocsp.key
answer "$work/revoked.pem" ocsp-revoked.der ca.pem ca.key
