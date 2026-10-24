#include <string.h>
#include "esp_camera.h"
#include "esp_log.h"
#include "esp_psram.h"
#include "sdkconfig.h"
#include "app.h"

static const char *TAG = "camera";

esp_err_t camera_start(void)
{
    bool psram = esp_psram_is_initialized();
    camera_config_t cfg = {
        .pin_pwdn = CONFIG_CAM_PIN_PWDN,
        .pin_reset = CONFIG_CAM_PIN_RESET,
        .pin_xclk = CONFIG_CAM_PIN_XCLK,
        .pin_sccb_sda = CONFIG_CAM_PIN_SIOD,
        .pin_sccb_scl = CONFIG_CAM_PIN_SIOC,
        .pin_d7 = CONFIG_CAM_PIN_D7,
        .pin_d6 = CONFIG_CAM_PIN_D6,
        .pin_d5 = CONFIG_CAM_PIN_D5,
        .pin_d4 = CONFIG_CAM_PIN_D4,
        .pin_d3 = CONFIG_CAM_PIN_D3,
        .pin_d2 = CONFIG_CAM_PIN_D2,
        .pin_d1 = CONFIG_CAM_PIN_D1,
        .pin_d0 = CONFIG_CAM_PIN_D0,
        .pin_vsync = CONFIG_CAM_PIN_VSYNC,
        .pin_href = CONFIG_CAM_PIN_HREF,
        .pin_pclk = CONFIG_CAM_PIN_PCLK,
        .xclk_freq_hz = CONFIG_CAM_XCLK_FREQ_HZ,
        .ledc_timer = LEDC_TIMER_0,
        .ledc_channel = LEDC_CHANNEL_0,
        // The model was trained on 96x96 grayscale, so ask the sensor for exactly that
        .pixel_format = PIXFORMAT_GRAYSCALE,
        .frame_size = FRAMESIZE_96X96,
        .jpeg_quality = 12,
        // A 96x96 frame is only 9 KB, so it fits in internal RAM if there is no PSRAM
        // (with PSRAM, a GC2145 is switched to 240x240 below: 56 KB per frame)
        .fb_count = psram ? 2 : 1,
        .fb_location = psram ? CAMERA_FB_IN_PSRAM : CAMERA_FB_IN_DRAM,
        .grab_mode = CAMERA_GRAB_LATEST,
    };

    esp_err_t err = esp_camera_init(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Camera init failed (0x%x). Check the ribbon cable and camera pins in menuconfig.", err);
        return err;
    }
    sensor_t *s = esp_camera_sensor_get();
    if (s->id.PID == GC2145_PID && psram) {
        // The GC2145 makes small sizes by cropping: 96x96 is a tiny zoomed-in patch of the
        // scene. Capture 240x240 (a much wider view) and shrink it in software. VGA would be
        // wider still, but is more data than the ESP32's camera input can take (it crashes).
        esp_camera_deinit();
        cfg.frame_size = FRAMESIZE_240X240;
        err = esp_camera_init(&cfg);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Camera init at 240x240 failed (0x%x)", err);
            return err;
        }
        s = esp_camera_sensor_get();
    }
    if (s->id.PID == GC2145_PID) {
        // The GC2145 has no grayscale mode and would keep sending RGB565. In YUV422 every
        // other byte is the brightness (Y), which is exactly the byte the driver keeps.
        s->set_pixformat(s, PIXFORMAT_YUV422);
    }
#if CONFIG_CAM_VFLIP
    s->set_vflip(s, 1);
#endif
    ESP_LOGI(TAG, "Camera ready (sensor PID 0x%x, frame buffers in %s)", s->id.PID, psram ? "PSRAM" : "internal RAM");
    return ESP_OK;
}

/* Grab a frame and convert pixels 0..255 to the model's signed -128..127 */
esp_err_t camera_grab_int8(int8_t *dst, int len)
{
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
        return ESP_FAIL;
    }
    if (fb->width == FRAME_W && fb->height == FRAME_H) {
        int n = fb->len < (size_t)len ? (int)fb->len : len;
        for (int i = 0; i < n; i++) {
            dst[i] = (int8_t)(fb->buf[i] ^ 0x80);
        }
        esp_camera_fb_return(fb);
        return n == len ? ESP_OK : ESP_ERR_INVALID_SIZE;
    }

    // Bigger frame (GC2145 at 240x240): take the centre square and average it down to 96x96
    if (len < FRAME_W * FRAME_H || fb->len < fb->width * fb->height) {
        esp_camera_fb_return(fb);
        return ESP_ERR_INVALID_SIZE;
    }
    int side = fb->width < fb->height ? fb->width : fb->height;
    int x0 = (fb->width - side) / 2, y0 = (fb->height - side) / 2;
    for (int y = 0; y < FRAME_H; y++) {
        int ya = y0 + y * side / FRAME_H, yb = y0 + (y + 1) * side / FRAME_H;
        for (int x = 0; x < FRAME_W; x++) {
            int xa = x0 + x * side / FRAME_W, xb = x0 + (x + 1) * side / FRAME_W;
            uint32_t sum = 0;
            for (int yy = ya; yy < yb; yy++) {
                const uint8_t *row = fb->buf + yy * fb->width;
                for (int xx = xa; xx < xb; xx++) {
                    sum += row[xx];
                }
            }
            uint8_t v = sum / ((yb - ya) * (xb - xa));
            dst[y * FRAME_W + x] = (int8_t)(v ^ 0x80);
        }
    }
    esp_camera_fb_return(fb);
    return ESP_OK;
}
