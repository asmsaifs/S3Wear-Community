# nanopb runtime, vendored from upstream tag nanopb-0.4.9.2
# (commit 160d4f09e5fabb2b66aa2dea32d4f38ace2c4b3f). See README.md.
idf_component_register(SRCS "src/pb_common.c" "src/pb_encode.c" "src/pb_decode.c"
                       INCLUDE_DIRS "include")
