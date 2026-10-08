// Console: `ble [on|off|reconnect|forget|pair yes|no]` — svc_ble state, Bluetooth on / off,
// bonds and pairing (P4-01, P4-09).
#include <stdio.h>
#include <string.h>

#include "esp_check.h"
#include "esp_console.h"
#include "svc_ble.h"
#include "svc_diag_priv.h"
#include "svc_link.h"
#include "svc_settings.h"

static const char *TAG = "cmd_ble";

static void print_addr(const char *what, const uint8_t *a, uint8_t type)
{
    printf("%s %02x:%02x:%02x:%02x:%02x:%02x (%s)\n", what, a[5], a[4], a[3], a[2], a[1], a[0],
           type == 0 ? "public" : "random");
}

static int status(void)
{
    svc_ble_status_t st;
    svc_ble_get_status(&st);
    printf("%s: %s%s%s, %u link(s), %s\n", st.name[0] ? st.name : "svc_ble", svc_ble_state_name(st.state),
           st.state == SVC_BLE_STATE_ADVERTISING ? (st.adv_fast ? " (fast)" : " (slow)") : "",
           st.enabled ? "" : " (Bluetooth off)", st.links, st.paired ? "paired" : "not paired");
    if (st.state != SVC_BLE_STATE_OFF) {
        print_addr("address", st.addr, st.addr_type);
    }
    if (st.paired) {
        print_addr("companion", st.peer, st.peer_type);
    }
    if (st.conn != 0xFFFF) {
        printf("companion link %u: MTU %u, notify TX %s, BULK %s\n", st.conn, st.mtu, st.tx_subscribed ? "on" : "off",
               st.bulk_subscribed ? "on" : "off");
    }
    if (st.pair_conn != 0xFFFF) {
        printf("pairing request waiting on link %u (ble pair yes|no)\n", st.pair_conn);
    }
    printf("since boot: %lu connects, %lu pairings, %lu frames in, %lu out\n", (unsigned long)st.connects,
           (unsigned long)st.pairings, (unsigned long)st.rx_frames, (unsigned long)st.tx_frames);
    svc_link_status_t ls;
    svc_link_get_status(&ls);
    printf("link: %lu messages in, %lu out, %lu dropped, %lu dups, %lu queue full\n", (unsigned long)ls.rx_messages,
           (unsigned long)ls.tx_messages, (unsigned long)ls.rx_dropped, (unsigned long)ls.rx_dups,
           (unsigned long)ls.queue_full);
    return 0;
}

static int check(esp_err_t err)
{
    if (err != ESP_OK) {
        printf("failed: %s\n", esp_err_to_name(err));
        return 1;
    }
    return 0;
}

static int cmd_ble(int argc, char **argv)
{
    if (argc == 1) {
        return status();
    }
    if (argc == 2 && (strcmp(argv[1], "on") == 0 || strcmp(argv[1], "off") == 0)) {
        return check(svc_settings_set_bool(S3W_SETTING_BLUETOOTH, argv[1][1] == 'n')); // svc_ble follows
    }
    if (argc == 2 && strcmp(argv[1], "reconnect") == 0) {
        return check(svc_ble_reconnect());
    }
    if (argc == 2 && strcmp(argv[1], "forget") == 0) {
        return check(svc_ble_forget());
    }
    if (argc == 3 && strcmp(argv[1], "pair") == 0 && (strcmp(argv[2], "yes") == 0 || strcmp(argv[2], "no") == 0)) {
        svc_ble_status_t st;
        svc_ble_get_status(&st);
        if (st.pair_conn == 0xFFFF) {
            printf("no pairing request\n");
            return 1;
        }
        return check(svc_ble_pair_reply(st.pair_conn, argv[2][0] == 'y'));
    }
    printf("usage: ble [on|off|reconnect|forget|pair yes|no]\n");
    return 1;
}

esp_err_t diag_register_ble(void)
{
    const esp_console_cmd_t cmd = {
        .command = "ble",
        .help = "svc_ble: state, address, companion bond and link (MTU, notifications), counters; "
                "on|off = the BLUETOOTH setting (off drops links, stops advertising); reconnect = fast "
                "advertising; forget = delete the bond and drop links; pair yes|no = answer a pairing request",
        .hint = "[on|off|reconnect|forget|pair yes|no]",
        .func = cmd_ble,
    };
    ESP_RETURN_ON_ERROR(esp_console_cmd_register(&cmd), TAG, "ble");
    return ESP_OK;
}
