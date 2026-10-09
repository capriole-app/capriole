#!/usr/bin/env bash
# Regenerates the signing test fixtures with OpenSSL (an external tool: its output is test data, the
# library is never linked). A throwaway test PKI — these keys protect nothing and are not secrets:
#   - ca.key / ca.pem          "Capriole Test CA" (RSA 2048), self-signed;
#   - rsa.p12                  "Mario Rossi (test)", RSA 2048, signed by the CA: PBES2/AES-256 + HMAC-SHA256
#                              (what OpenSSL 3 writes), password "capriole", with the CA certificate;
#   - ec.p12                   "Anna Bianchi (test)", ECDSA P-256, the same protection;
#   - legacy.p12               the RSA key and certificate as older tools write them (RC2-40 and 3DES,
#                              HMAC-SHA1: openssl -legacy), password "capriole";
#   - nopass.p12               the ECDSA key with an empty password;
#   - other.pem                a CA that signed none of them (an untrusted anchor);
#   - tsa.key / tsa.pem / tsa.cnf  a time-stamping authority signed by the CA (openssl ts -reply);
#   - timestamp.tsr            its response for a time stamp of the bytes "capriole";
#   - and the revocation fixtures of make_ltv_fixtures.sh (OCSP, CRL, a revoked signer).
set -euo pipefail
cd "$(dirname "$0")"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

days=36500
openssl req -x509 -newkey rsa:2048 -nodes -keyout ca.key -out ca.pem -days $days -subj "/C=IT/O=Capriole/CN=Capriole Test CA" \
  -addext "basicConstraints=critical,CA:TRUE" -addext "keyUsage=critical,keyCertSign,cRLSign" -set_serial 1 2>/dev/null
openssl req -x509 -newkey rsa:2048 -nodes -keyout "$work/other.key" -out other.pem -days $days -subj "/CN=Another Test CA" \
  -addext "basicConstraints=critical,CA:TRUE" -set_serial 1 2>/dev/null

issue() {  # name, key algorithm options, subject, serial, extensions
  openssl req -new -newkey "$2" -nodes -keyout "$work/$1.key" -out "$work/$1.csr" -subj "$3" 2>/dev/null
  printf '%s\n' "$5" > "$work/$1.ext"
  openssl x509 -req -in "$work/$1.csr" -CA ca.pem -CAkey ca.key -set_serial "$4" -days $days -out "$work/$1.pem" -extfile "$work/$1.ext" 2>/dev/null
}
signer_ext=$'basicConstraints=critical,CA:FALSE\nkeyUsage=critical,digitalSignature,nonRepudiation'
issue rsa rsa:2048 "/C=IT/O=Capriole/CN=Mario Rossi (test)" 2 "$signer_ext"
openssl ecparam -name prime256v1 -out "$work/p256.pem"
issue ec "ec:$work/p256.pem" "/C=IT/O=Capriole/CN=Anna Bianchi (test)" 3 "$signer_ext"
issue tsa rsa:2048 "/C=IT/O=Capriole/CN=Capriole Test TSA" 4 $'basicConstraints=critical,CA:FALSE\nkeyUsage=critical,digitalSignature\nextendedKeyUsage=critical,timeStamping'
cp "$work/tsa.key" tsa.key
cp "$work/tsa.pem" tsa.pem

openssl pkcs12 -export -inkey "$work/rsa.key" -in "$work/rsa.pem" -certfile ca.pem -name "Mario Rossi" -passout pass:capriole -out rsa.p12
openssl pkcs12 -export -inkey "$work/ec.key" -in "$work/ec.pem" -certfile ca.pem -name "Anna Bianchi" -passout pass:capriole -out ec.p12
openssl pkcs12 -export -legacy -inkey "$work/rsa.key" -in "$work/rsa.pem" -certfile ca.pem -passout pass:capriole -out legacy.p12
openssl pkcs12 -export -inkey "$work/ec.key" -in "$work/ec.pem" -passout pass: -out nopass.p12

cat > tsa.cnf <<'CNF'
[ tsa ]
default_tsa = tsa_config
[ tsa_config ]
serial = ./tsa-serial
crypto_device = builtin
signer_digest = sha256
default_policy = 1.3.6.1.4.1.99999.1
digests = sha256, sha384, sha512
accuracy = secs:1
ordering = no
tsa_name = no
ess_cert_id_chain = no
ess_cert_id_alg = sha256
CNF

# A time stamp of the bytes "capriole" (as if they were a signature value), from the test TSA.
printf capriole > "$work/value"
openssl ts -query -data "$work/value" -sha256 -cert -out "$work/query.tsq" 2>/dev/null
echo 01 > "$work/tsa-serial"
(cd "$work" && openssl ts -reply -queryfile query.tsq -inkey "$OLDPWD/tsa.key" -signer "$OLDPWD/tsa.pem" -chain "$OLDPWD/ca.pem" \
  -config "$OLDPWD/tsa.cnf" -out "$OLDPWD/timestamp.tsr" 2>/dev/null)

# The revocation fixtures, under the CA just made.
./make_ltv_fixtures.sh
