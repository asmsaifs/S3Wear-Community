# Watch faces (P3-02..P3-04): engine, complication and binding registries, native and
# declarative faces, picker and customize. Portable (LVGL + ui_framework only): the
# simulator builds the same list.
include(${CMAKE_CURRENT_LIST_DIR}/sources.cmake)

idf_component_register(SRCS ${S3W_WF_SRCS}
                       INCLUDE_DIRS "include"
                       PRIV_INCLUDE_DIRS "."
                       REQUIRES lvgl ui_framework
                       PRIV_REQUIRES heap)
