# Signing test fixtures

A throwaway test PKI made by `make_fixtures.sh` (and, under the same CA, `make_ltv_fixtures.sh`: an
OCSP responder, a signer whose certificate says where to ask, a revoked one, the CA's CRL and
database, OCSP answers): the keys (`*.key`, `*.p12`, passwords in the script)
protect nothing and are **not secrets**. Do not trust these certificates anywhere but in the tests.
