#pragma once
#include <stdbool.h>

/* What the report found, so it can be sent to the dashboard too. */
typedef struct {
    unsigned flash_mb;
    unsigned psram_mb;   // 0 = no PSRAM
    char camera[16];     // sensor model, e.g. "OV2640"; empty = no camera
    bool mic;
} board_info_t;

/* Print what this chip can find out about itself and its board (chip, memory, flash,
 * camera, microphone, Wi-Fi, tasks, restart counters). Call once at boot. */
void board_report(board_info_t *info);
