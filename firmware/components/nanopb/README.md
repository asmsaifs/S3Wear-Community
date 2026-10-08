# nanopb (vendored)

Runtime sources of [nanopb](https://github.com/nanopb/nanopb) (zlib licence, see `LICENSE.txt`).

| | |
|---|---|
| Version | `nanopb-0.4.9.2` |
| Upstream commit | `160d4f09e5fabb2b66aa2dea32d4f38ace2c4b3f` |
| Files | `pb.h`, `pb_common.{h,c}`, `pb_encode.{h,c}`, `pb_decode.{h,c}`, `LICENSE.txt` — unmodified |

Vendored instead of pulled from the ESP Component Registry because there is no
Espressif- or nanopb-maintained package there; the third-party ones either set
nanopb options (`PB_BUFFER_ONLY`, ...) as private defines, which changes
`pb_istream_t` layout between the library and generated code, or are unmaintained.

The code generator used by `tools/gen_proto.sh` must be the same version. 0.4.9.2 is not on PyPI, so the
script runs `generator/nanopb_generator.py` from the upstream tag tarball (SHA-256 pinned in the script).
Compile-time options (`PB_ENABLE_MALLOC`, `PB_FIELD_32BIT`, ...) must be set here as
`PUBLIC` compile definitions so the library and generated code agree.

To update: replace the files from the new tag, update this table and the generator pin.
