/*
 * Workshop app 03: Person Detector
 *
 *   camera (96x96 gray) -> TFLite Micro model -> person yes/no
 *        -> status LED + UDP broadcast {team, person, seen...} -> dashboard
 *
 * Images never leave the board. Only the result goes over Wi-Fi.
 */
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "sdkconfig.h"
#include "model_settings.h"
#include "app.h"

static const char *TAG = "main";

#define PERSON_ON_FRAMES  2   // consecutive person frames before we say "person"
#define PERSON_OFF_FRAMES 3   // consecutive empty frames before we say "gone"

static detect_state_t s_state;
static uint8_t *s_last_frame;   // copy of what the model last saw (`snap`, /stream), in PSRAM if present
static uint32_t s_frame_seq;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

void state_get(detect_state_t *out)
{
    portENTER_CRITICAL(&s_lock);
    *out = s_state;
    portEXIT_CRITICAL(&s_lock);
}

/* Print the last 96x96 grayscale frame as hex, one row per line.
 * tools/snap_to_png.py turns this back into an image. */
uint32_t frame_copy(uint8_t *dst)
{
    portENTER_CRITICAL(&s_lock);
    if (s_last_frame) {
        memcpy(dst, s_last_frame, kMaxImageSize);
    }
    uint32_t seq = s_frame_seq;
    portEXIT_CRITICAL(&s_lock);
    return seq;
}

void frame_print(void)
{
    uint8_t *copy = heap_caps_malloc_prefer(kMaxImageSize, 2, MALLOC_CAP_SPIRAM, MALLOC_CAP_8BIT);
    if (!copy) {
        printf("out of memory\n");
        return;
    }
    frame_copy(copy);

    printf("\n-----BEGIN FRAME %dx%d-----\n", kNumCols, kNumRows);
    for (int y = 0; y < kNumRows; y++) {
        for (int x = 0; x < kNumCols; x++) {
            printf("%02x", copy[y * kNumCols + x]);
        }
        printf("\n");
    }
    printf("-----END FRAME-----\n");
    free(copy);
}

static void led_set(bool on)
{
#if CONFIG_WORKSHOP_LED_GPIO >= 0
#if CONFIG_WORKSHOP_LED_ACTIVE_LOW
    on = !on;
#endif
    gpio_set_level(CONFIG_WORKSHOP_LED_GPIO, on);
#endif
}

/* Debounce the raw per-frame score into a stable person / no-person state */
void state_update(int score, uint32_t infer_ms)
{
    static int streak_on, streak_off;
    bool raw = score >= CONFIG_WORKSHOP_PERSON_THRESHOLD;
    streak_on = raw ? streak_on + 1 : 0;
    streak_off = raw ? 0 : streak_off + 1;

    bool changed = false;
    portENTER_CRITICAL(&s_lock);
    s_state.score = score;
    s_state.infer_ms = infer_ms;
    if (!s_state.person && streak_on >= PERSON_ON_FRAMES) {
        s_state.person = true;
        s_state.seen++;
        changed = true;
    } else if (s_state.person && streak_off >= PERSON_OFF_FRAMES) {
        s_state.person = false;
        changed = true;
    }
    detect_state_t now = s_state;
    portEXIT_CRITICAL(&s_lock);

    if (changed) {
        led_set(now.person);
        if (now.person) {
            ESP_LOGI(TAG, ">>> PERSON detected (score %d%%, seen %lu times)", score, (unsigned long)now.seen);
        } else {
            ESP_LOGI(TAG, "<<< no person");
        }
        net_notify();
    }
}

static void detect_task(void *arg)
{
    int8_t *input = detector_input();
    int64_t last_log = 0;
    uint32_t frames = 0;
    while (1) {
        if (camera_grab_int8(input, kMaxImageSize) != ESP_OK) {
            ESP_LOGW(TAG, "Camera frame failed");
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        portENTER_CRITICAL(&s_lock);
        for (int i = 0; s_last_frame && i < kMaxImageSize; i++) {
            s_last_frame[i] = (uint8_t)input[i] ^ 0x80;
        }
        s_frame_seq++;
        portEXIT_CRITICAL(&s_lock);

        int64_t t0 = esp_timer_get_time();
        int score = 0;
        if (detector_run(&score) != ESP_OK) {
            ESP_LOGE(TAG, "Inference failed");
            continue;
        }
        uint32_t ms = (uint32_t)((esp_timer_get_time() - t0) / 1000);
        state_update(score, ms);

        frames++;
        int64_t now = esp_timer_get_time();
        if (now - last_log > 5 * 1000 * 1000) {
            uint32_t fps_x10 = last_log ? (uint32_t)(frames * 10000000LL / (now - last_log)) : 0;
            portENTER_CRITICAL(&s_lock);
            s_state.fps_x10 = fps_x10;
            portEXIT_CRITICAL(&s_lock);
            frames = 0;
            last_log = now;
            ESP_LOGI(TAG, "[%s] person score %3d%%  inference %lu ms  %lu.%lu fps", g_config.team, score,
                     (unsigned long)ms, (unsigned long)(fps_x10 / 10), (unsigned long)(fps_x10 % 10));
        }
        vTaskDelay(1);   // let lower-priority tasks (and the watchdog) breathe
    }
}

void app_main(void)
{
    printf("\n=== Person Detector " FW_VERSION " (%s) ===\n", CONFIG_IDF_TARGET);

#if CONFIG_WORKSHOP_LED_GPIO >= 0
    gpio_reset_pin(CONFIG_WORKSHOP_LED_GPIO);
    gpio_set_direction(CONFIG_WORKSHOP_LED_GPIO, GPIO_MODE_OUTPUT);
    led_set(false);
#endif

    config_load();
    s_last_frame = heap_caps_calloc_prefer(kMaxImageSize, 1, 2, MALLOC_CAP_SPIRAM, MALLOC_CAP_8BIT);

    // Model memory first, so it gets fast internal RAM before Wi-Fi takes its share
    bool ok = detector_init() == ESP_OK && camera_start() == ESP_OK;
    net_start();
    if (!ok) {
        ESP_LOGE(TAG, "Setup failed - see the error above. Console still works.");
        console_start();
        return;
    }

    // Inference on core 1, Wi-Fi mostly lives on core 0
    xTaskCreatePinnedToCore(detect_task, "detect", 8192, NULL, 5, NULL, 1);
    stream_start();
    ESP_LOGI(TAG, "Free internal RAM: %u KB, PSRAM: %u KB",
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));
    console_start();
    printf("\nType `team <your team name>` and press Enter. `help` lists commands.\n\n");
}
