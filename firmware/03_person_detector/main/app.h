#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TEAM_MAX_LEN 24

/* config.c - settings stored in the NVS partition (namespace "workshop") */
typedef struct {
    char team[TEAM_MAX_LEN + 1];
    char ssid[33];
    char pass[65];
} app_config_t;

extern app_config_t g_config;

void config_load(void);
esp_err_t config_set_team(const char *team);
esp_err_t config_set_wifi(const char *ssid, const char *pass);
bool config_team_valid(const char *team);

/* Live detector state, shared by the detector and the network reporter */
typedef struct {
    bool person;
    int score;          // 0..100
    uint32_t seen;      // number of "no person -> person" transitions
    uint32_t infer_ms;  // last inference time
    uint32_t fps_x10;   // frames per second x10 (camera + inference loop)
} detect_state_t;

void state_get(detect_state_t *out);
void state_update(int score, uint32_t infer_ms);   // main.c
void frame_print(void);                             // main.c: dump last model input for `snap`
uint32_t frame_copy(uint8_t *dst);                  // main.c: copy last 96x96 frame, returns frame number
#define FRAME_W 96
#define FRAME_H 96

/* stream.c - HTTP server: /stream (MJPEG) and /frame.jpg */
void stream_start(void);

/* net.c */
void net_start(void);
void net_notify(void);   // send a packet now (called on state change)

/* camera.c */
esp_err_t camera_start(void);
esp_err_t camera_grab_int8(int8_t *dst, int len);

/* detector.cc */
esp_err_t detector_init(void);
int8_t *detector_input(void);
esp_err_t detector_run(int *person_score);

/* console.c */
void console_start(void);

#ifdef __cplusplus
}
#endif
