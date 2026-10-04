// Mbed TLS configuration for Capriole (PLAN.md phase 8h): only what signatures need — X.509
// certificates, RSA (PKCS#1 v1.5 and PSS) and ECDSA, the hashes they use, and what reads a PKCS#12
// file (PBES2/PBKDF2 with AES, the PKCS#12 PBE with 3DES). No TLS, no PSA, no platform entropy, no
// clock: randomness (key blinding) comes from a deterministic HMAC-DRBG the engine seeds, and
// certificate validity is checked by the engine against the signing time, not the current one. PEM
// is read for trust anchors (a CA bundle).
#ifndef CAPRIOLE_MBEDTLS_CONFIG_H
#define CAPRIOLE_MBEDTLS_CONFIG_H

#define MBEDTLS_NO_PLATFORM_ENTROPY

#define MBEDTLS_ASN1_PARSE_C
#define MBEDTLS_ASN1_WRITE_C
#define MBEDTLS_BIGNUM_C
#define MBEDTLS_OID_C

#define MBEDTLS_MD_C
#define MBEDTLS_MD5_C
#define MBEDTLS_SHA1_C
#define MBEDTLS_SHA224_C
#define MBEDTLS_SHA256_C
#define MBEDTLS_SHA384_C
#define MBEDTLS_SHA512_C
#define MBEDTLS_HMAC_DRBG_C

#define MBEDTLS_RSA_C
#define MBEDTLS_PKCS1_V15
#define MBEDTLS_PKCS1_V21
#define MBEDTLS_ECP_C
#define MBEDTLS_ECP_NIST_OPTIM
#define MBEDTLS_ECP_DP_SECP256R1_ENABLED
#define MBEDTLS_ECP_DP_SECP384R1_ENABLED
#define MBEDTLS_ECP_DP_SECP521R1_ENABLED
#define MBEDTLS_ECP_DP_BP256R1_ENABLED
#define MBEDTLS_ECP_DP_BP384R1_ENABLED
#define MBEDTLS_ECP_DP_BP512R1_ENABLED
#define MBEDTLS_ECDSA_C
#define MBEDTLS_ECDSA_DETERMINISTIC

#define MBEDTLS_PK_C
#define MBEDTLS_PK_PARSE_C
#define MBEDTLS_PK_WRITE_C
#define MBEDTLS_X509_USE_C
#define MBEDTLS_X509_CRT_PARSE_C
#define MBEDTLS_X509_RSASSA_PSS_SUPPORT
#define MBEDTLS_BASE64_C
#define MBEDTLS_PEM_PARSE_C

#define MBEDTLS_CIPHER_C
#define MBEDTLS_CIPHER_MODE_CBC
#define MBEDTLS_CIPHER_PADDING_PKCS7
#define MBEDTLS_AES_C
#define MBEDTLS_DES_C
#define MBEDTLS_PKCS5_C
#define MBEDTLS_PKCS12_C

#endif  // CAPRIOLE_MBEDTLS_CONFIG_H
