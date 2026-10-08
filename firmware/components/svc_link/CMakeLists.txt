# S3W Link: framing, request/reply, bulk transfers over svc_ble (docs/06; P4-02).
# link_frame.c, link_rpc.c and link_bulk.c are pure C and also built by firmware/host_test.
idf_component_register(SRCS "svc_link.c" "link_frame.c" "link_rpc.c" "link_bulk.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_event proto
                       PRIV_REQUIRES sys_core svc_ble nanopb esp_timer log)
