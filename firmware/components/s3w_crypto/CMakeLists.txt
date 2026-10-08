# SHA-256 (s3w_sha256.c) and Ed25519 (Monocypher 4.0.2, vendored: see README.md). Pure C;
# firmware/host_test and the simulator build the same files (sources.cmake).
include(${CMAKE_CURRENT_LIST_DIR}/sources.cmake)
idf_component_register(SRCS ${S3W_CRYPTO_SRCS}
                       INCLUDE_DIRS ${S3W_CRYPTO_INCLUDE_DIRS})
# Monocypher is tuned for speed by default; -O2 for the verify on the watch even in -Og dev builds.
target_compile_options(${COMPONENT_LIB} PRIVATE -O2)
