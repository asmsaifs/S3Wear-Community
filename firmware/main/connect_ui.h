// Settings > Connections glue (P4-09): the connect_apps backend over svc_settings, svc_ble,
// svc_link and link_time, and their events to the page.
#pragma once

/** UI task (or lv_lock held), after shell_init(), svc_ble_start() and svc_link_start(). */
void connect_ui_start(void);
