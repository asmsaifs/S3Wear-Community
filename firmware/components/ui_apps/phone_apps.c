// Phone link UI: the pairing code alert (phone_apps.h, docs/04-ui-ux.md §4f).
#include "phone_apps.h"

#include <stdio.h>

#include "ui_overlay.h"

static struct {
    bool open;      // the alert is shown or waiting, unanswered
    bool rejected;  // the user said no: no "failed" toast
    phone_pair_reply_t reply;
    void *ctx;
} s;

static void pair_result(int button, void *ctx)
{
    (void)ctx;
    s.open = false;
    const bool accept = button == 0;
    s.rejected = !accept;
    if (s.reply) {
        s.reply(accept, s.ctx);
    }
}

void phone_apps_pair_request(uint32_t code, bool replaces, phone_pair_reply_t reply, void *ctx)
{
    if (s.open) {
        ui_alert_cancel(pair_result, NULL);
    }
    s.open = true;
    s.rejected = false;
    s.reply = reply;
    s.ctx = ctx;
    char title[16];
    snprintf(title, sizeof title, "%03lu %03lu", (unsigned long)(code / 1000 % 1000), (unsigned long)(code % 1000));
    const ui_alert_t a = {
        .icon = LV_SYMBOL_BLUETOOTH,
        .title = title,
        .body = replaces ? "Pair with this phone? It replaces the phone paired now. Check it shows the same code."
                         : "Pair with this phone? Check it shows the same code.",
        .primary = "Pair",
        .secondary = "Cancel",
        .on_result = pair_result,
        .prio = UI_ALERT_PRIO_NORMAL,
        .back_dismisses = true,
    };
    ui_alert_show(&a);
}

void phone_apps_pair_done(bool ok)
{
    if (s.open) {
        ui_alert_cancel(pair_result, NULL);
        s.open = false;
    }
    if (ok) {
        ui_toast_show("Phone paired", 0);
    } else if (!s.rejected) {
        ui_toast_show("Pairing failed", 0);
    }
    s.rejected = false;
}
