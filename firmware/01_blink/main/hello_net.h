#pragma once
#include "esp_err.h"
#include "board_report.h"

#define TEAM_MAX_LEN 24

/* Start the Wi-Fi radio (station mode). Safe to call more than once. */
esp_err_t wifi_start_once(void);

/* Join the workshop Wi-Fi and tell the dashboard about this board every 2 s. */
void hello_net_start(const board_info_t *info);

/* Team name and Wi-Fi settings, stored in flash (NVS namespace "workshop",
 * the same one the Person Detector uses, so the team name carries over). */
const char *team_get(void);
esp_err_t team_set(const char *team);
esp_err_t wifi_settings_set(const char *ssid, const char *pass);

/* console.c: type `team <name>` in the serial console */
void console_start(void);
