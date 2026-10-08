# s3w_crypto sources, shared by the watch build (CMakeLists.txt), firmware/host_test and the simulator.
set(S3W_CRYPTO_SRCS
    ${CMAKE_CURRENT_LIST_DIR}/s3w_sha256.c
    ${CMAKE_CURRENT_LIST_DIR}/monocypher/monocypher.c
    ${CMAKE_CURRENT_LIST_DIR}/monocypher/monocypher-ed25519.c)
set(S3W_CRYPTO_INCLUDE_DIRS
    ${CMAKE_CURRENT_LIST_DIR}/include
    ${CMAKE_CURRENT_LIST_DIR}/monocypher)
