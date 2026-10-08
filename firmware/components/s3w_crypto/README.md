# s3w_crypto

- `s3w_sha256.{h,c}`: SHA-256, our own (FIPS 180-4), host-tested against the NIST vectors (`test_app_pkg`).
- `monocypher/`: [Monocypher](https://monocypher.org) (BSD-2-Clause or CC0, see `monocypher/LICENCE.md`),
  vendored for Ed25519 (`crypto_ed25519_check`, RFC 8032, with SHA-512) — mbedTLS in ESP-IDF 5.5 has no Ed25519.

| | |
|---|---|
| Version | `4.0.2` |
| Source | https://monocypher.org/download/monocypher-4.0.2.tar.gz (SHA-256 `38d07179738c0c90677dba3ceb7a7b8496bcfea758ba1a53e803fed30ae0879c`) |
| Files | `src/monocypher.{h,c}`, `src/optional/monocypher-ed25519.{h,c}`, `LICENCE.md` — unmodified |

Used by: app package signatures (`app_runtime/app_pkg.c`, docs/05 §3) and the Pro licence (`svc_license/license_file.c`, docs/10 §4). To update: replace the files from the
new release, update this table, run the host tests (`AppPkg.Ed25519Rfc8032`).
