/*
 * Board report: everything this chip can find out about itself and its board.
 * Printed once at every boot, before the LED starts blinking.
 */
#include <stdio.h>
#include <inttypes.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_mac.h"
#include "esp_memory_utils.h"
#include "esp_partition.h"
#include "esp_psram.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "esp_camera.h"
#include "driver/i2s_std.h"
#include "soc/soc.h"
#include "sdkconfig.h"
#include "board_report.h"
#include "hello_net.h"

static board_info_t *s_info;   // filled in as we go, sent to the dashboard later

/* ---------- things placed in different memories, on purpose ---------- */
static const char greeting_in_flash[] = "I am a const string";   // flash (read-only data)
static int counter_in_ram = 42;                                   // internal RAM
static void function_in_flash(void) { }                           // runs straight from flash

static const char *where(const void *p)
{
    uintptr_t a = (uintptr_t)p;
    if (esp_ptr_external_ram(p))                 return "PSRAM (external RAM)";
    if (esp_ptr_in_iram(p))                      return "internal RAM (code)";
    if (esp_ptr_in_dram(p))                      return "internal RAM (data)";
    if (a >= SOC_IROM_LOW && a < SOC_IROM_HIGH)  return "FLASH (code)";
    if (a >= SOC_DROM_LOW && a < SOC_DROM_HIGH)  return "FLASH (read-only data)";
    if (esp_ptr_in_rom(p))                       return "ROM (inside the chip)";
    return "?";
}

static void title(const char *t)
{
    printf("\n------------------------------------------------------------\n %s\n", t);
    printf("------------------------------------------------------------\n");
}

static const char *reset_reason_str(esp_reset_reason_t r)
{
    switch (r) {
    case ESP_RST_POWERON:  return "power-on (just plugged in)";
    case ESP_RST_EXT:      return "RST button";
    case ESP_RST_SW:       return "software restart (BOOT button)";
    case ESP_RST_PANIC:    return "crash";
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT:      return "watchdog";
    case ESP_RST_BROWNOUT: return "brownout (power dipped)";
    case ESP_RST_USB:      return "USB (the PC / flasher reset it)";
    case ESP_RST_JTAG:     return "JTAG debugger";
    default:               return "other";
    }
}

/* ---------------------------------------------------------------- chip */
static void report_chip(void)
{
    esp_chip_info_t chip;
    esp_chip_info(&chip);
    uint32_t flash_size = 0;
    esp_flash_get_size(NULL, &flash_size);
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);

    title("CHIP");
    printf(" Model      : %s, silicon v%d.%d\n", CONFIG_IDF_TARGET, chip.revision / 100, chip.revision % 100);
    printf(" CPU        : %d cores @ %d MHz\n", chip.cores, CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ);
    printf(" Radio      : %s%s\n", (chip.features & CHIP_FEATURE_WIFI_BGN) ? "Wi-Fi 2.4 GHz  " : "",
           (chip.features & CHIP_FEATURE_BLE) ? "Bluetooth LE" : "");
    printf(" Flash      : %" PRIu32 " MB  (outside the chip, on SPI)\n", flash_size / (1024 * 1024));
    s_info->flash_mb = flash_size / (1024 * 1024);
    if (esp_psram_is_initialized()) {
        s_info->psram_mb = esp_psram_get_size() / (1024 * 1024);
        printf(" PSRAM      : %u MB  (outside the chip, on SPI)\n", (unsigned)(esp_psram_get_size() / (1024 * 1024)));
    } else {
        printf(" PSRAM      : none found\n");
    }
    printf(" Free RAM   : %u KB internal\n", (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024));
    printf(" MAC        : %02x:%02x:%02x:%02x:%02x:%02x  (unique to this chip)\n",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    printf(" Last reset : %s\n", reset_reason_str(esp_reset_reason()));
}

/* ---------------------------------------------------------------- memory */
static void report_memory(void)
{
    int on_stack = 0;
    void *heap = malloc(64);

    title("WHERE THINGS LIVE (address -> memory)");
    printf(" %-22s 0x%08" PRIxPTR "  %s\n", "const string", (uintptr_t)greeting_in_flash, where(greeting_in_flash));
    printf(" %-22s 0x%08" PRIxPTR "  %s\n", "global variable", (uintptr_t)&counter_in_ram, where(&counter_in_ram));
    printf(" %-22s 0x%08" PRIxPTR "  %s\n", "local variable", (uintptr_t)&on_stack, where(&on_stack));
    printf(" %-22s 0x%08" PRIxPTR "  %s\n", "malloc()", (uintptr_t)heap, where(heap));
    printf(" %-22s 0x%08" PRIxPTR "  %s\n", "a function", (uintptr_t)&function_in_flash, where((void *)&function_in_flash));
    printf(" %-22s 0x%08" PRIxPTR "  %s\n", "memcpy()", (uintptr_t)&memcpy, where((void *)&memcpy));
    free(heap);
}

/* ---------------------------------------------------------------- flash layout */
static void report_partitions(void)
{
    title("FLASH PARTITIONS");
    printf(" %-10s %-5s %-10s %s\n", "name", "type", "offset", "size");
    esp_partition_iterator_t it = esp_partition_find(ESP_PARTITION_TYPE_ANY, ESP_PARTITION_SUBTYPE_ANY, NULL);
    for (; it != NULL; it = esp_partition_next(it)) {
        const esp_partition_t *p = esp_partition_get(it);
        printf(" %-10s %-5s 0x%06" PRIx32 "   %5" PRIu32 " KB\n", p->label,
               p->type == ESP_PARTITION_TYPE_APP ? "app" : "data", p->address, p->size / 1024);
    }
    esp_partition_iterator_release(it);
}

/* ---------------------------------------------------------------- camera */
#if CONFIG_PROBE_CAMERA
static const char *sensor_name(uint16_t pid)
{
    switch (pid) {
    case OV2640_PID: return "OV2640 (2 MP)";
    case OV3660_PID: return "OV3660 (3 MP)";
    case OV5640_PID: return "OV5640 (5 MP)";
    case OV7725_PID: return "OV7725";
    case GC2145_PID: return "GC2145 (2 MP)";
    case GC032A_PID: return "GC032A";
    case GC0308_PID: return "GC0308";
    case BF3005_PID: return "BF3005";
    case SC030IOT_PID: return "SC030IOT";
    default: return "unknown model";
    }
}

static void report_camera(void)
{
    title("CAMERA");
    // the camera driver is chatty; silence all logs while probing, print our own summary
    esp_log_level_set("*", ESP_LOG_NONE);

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
        .pixel_format = PIXFORMAT_GRAYSCALE,
        .frame_size = FRAMESIZE_96X96,
        .fb_count = 1,
        .fb_location = psram ? CAMERA_FB_IN_PSRAM : CAMERA_FB_IN_DRAM,
        .grab_mode = CAMERA_GRAB_LATEST,
    };
    esp_err_t err = esp_camera_init(&cfg);
    esp_log_level_set("*", ESP_LOG_INFO);
    if (err != ESP_OK) {
        printf(" Not found  (no camera answered on SDA %d / SCL %d)\n", CONFIG_CAM_PIN_SIOD, CONFIG_CAM_PIN_SIOC);
        printf("            Check the ribbon cable, or the camera pins in menuconfig.\n");
        return;
    }
    sensor_t *s = esp_camera_sensor_get();
    if (s->id.PID == GC2145_PID) {
        s->set_pixformat(s, PIXFORMAT_YUV422);   // no grayscale mode: Y bytes of YUV422 are the gray image
    }
    printf(" Found      : %s, talks over I2C (SCCB) at address 0x%02x\n", sensor_name(s->id.PID), s->slv_addr);
    // keep just the model ("OV2640") for the dashboard
    strlcpy(s_info->camera, sensor_name(s->id.PID), sizeof(s_info->camera));
    s_info->camera[strcspn(s_info->camera, " ")] = '\0';

    // throw away the first frames while auto-exposure settles, then measure brightness
    camera_fb_t *fb = NULL;
    for (int i = 0; i < 6; i++) {
        if (fb) {
            esp_camera_fb_return(fb);
        }
        fb = esp_camera_fb_get();
    }
    if (fb) {
        uint32_t sum = 0;
        for (size_t i = 0; i < fb->len; i++) {
            sum += fb->buf[i];
        }
        unsigned avg = fb->len ? (unsigned)(sum / fb->len) : 0;
        printf(" Picture    : %ux%u grayscale, %u bytes, average brightness %u / 255\n",
               (unsigned)fb->width, (unsigned)fb->height, (unsigned)fb->len, avg);
        printf("              %s\n", avg < 40 ? "(dark: is the lens covered?)" : "(try covering the lens and press RST)");
        esp_camera_fb_return(fb);
    } else {
        printf(" Picture    : camera answered but sent no frame\n");
    }
    esp_camera_deinit();
}
#endif

/* ---------------------------------------------------------------- microphone */
#if CONFIG_PROBE_MIC
static void report_mic(void)
{
    title("MICROPHONE");
    i2s_chan_handle_t rx = NULL;
    i2s_chan_config_t chan = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    i2s_std_config_t std = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(16000),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = CONFIG_MIC_PIN_BCLK,
            .ws = CONFIG_MIC_PIN_WS,
            .dout = I2S_GPIO_UNUSED,
            .din = CONFIG_MIC_PIN_DIN,
        },
    };
    std.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT;
    if (i2s_new_channel(&chan, NULL, &rx) != ESP_OK || i2s_channel_init_std_mode(rx, &std) != ESP_OK ||
        i2s_channel_enable(rx) != ESP_OK) {
        printf(" Could not start I2S on BCLK %d / WS %d / DATA %d\n", CONFIG_MIC_PIN_BCLK, CONFIG_MIC_PIN_WS, CONFIG_MIC_PIN_DIN);
        if (rx) {
            i2s_del_channel(rx);
        }
        return;
    }

    // listen for ~0.5 s, skip the first 0.1 s while the mic wakes up
    const int n = 1024;
    int32_t *buf = malloc(n * sizeof(int32_t));
    int64_t peak = 0, first = 0;
    bool varies = false, have_first = false;
    for (int round = 0; buf && round < 8; round++) {
        size_t got = 0;
        if (i2s_channel_read(rx, buf, n * sizeof(int32_t), &got, pdMS_TO_TICKS(500)) != ESP_OK) {
            break;
        }
        if (round < 2) {
            continue;
        }
        for (size_t i = 0; i < got / sizeof(int32_t); i++) {
            int64_t v = buf[i] >> 8;   // 24-bit sample in the top bits
            if (!have_first) {
                first = v;
                have_first = true;
            } else if (v != first) {
                varies = true;
            }
            if (llabs(v) > peak) {
                peak = llabs(v);
            }
        }
    }
    free(buf);
    i2s_channel_disable(rx);
    i2s_del_channel(rx);

    if (!varies) {
        printf(" Not found  (no sound data on BCLK %d / WS %d / DATA %d)\n", CONFIG_MIC_PIN_BCLK, CONFIG_MIC_PIN_WS, CONFIG_MIC_PIN_DIN);
        return;
    }
    int bars = 0;
    for (int64_t p = peak; p > 255 && bars < 30; p >>= 1) {
        bars += 2;   // rough log scale
    }
    printf(" Found      : I2S digital mic (BCLK %d / WS %d / DATA %d)\n", CONFIG_MIC_PIN_BCLK, CONFIG_MIC_PIN_WS, CONFIG_MIC_PIN_DIN);
    s_info->mic = true;
    printf(" Sound level: [%-30.*s]  (clap and press RST!)\n", bars, "##############################");
}
#endif

/* ---------------------------------------------------------------- Wi-Fi */
#if CONFIG_PROBE_WIFI
static int cmp_rssi(const void *a, const void *b)
{
    return ((const wifi_ap_record_t *)b)->rssi - ((const wifi_ap_record_t *)a)->rssi;
}

static void report_wifi(void)
{
    title("WI-FI SCAN (the radio is working!)");
    esp_log_level_set("wifi", ESP_LOG_WARN);
    esp_log_level_set("wifi_init", ESP_LOG_WARN);
    esp_log_level_set("phy_init", ESP_LOG_WARN);
    esp_log_level_set("pp", ESP_LOG_WARN);
    esp_log_level_set("net80211", ESP_LOG_WARN);

    if (wifi_start_once() != ESP_OK) {
        printf(" Wi-Fi did not start\n");
        return;
    }
    if (esp_wifi_scan_start(NULL, true) != ESP_OK) {
        printf(" Scan failed\n");
        return;
    }
    uint16_t found = 0;
    esp_wifi_scan_get_ap_num(&found);
    uint16_t n = found;
    wifi_ap_record_t *aps = calloc(n ? n : 1, sizeof(wifi_ap_record_t));
    if (!aps) {
        esp_wifi_clear_ap_list();
        return;
    }
    esp_wifi_scan_get_ap_records(&n, aps);
    qsort(aps, n, sizeof(wifi_ap_record_t), cmp_rssi);
    printf(" Networks nearby: %u\n", (unsigned)found);
    for (int i = 0; i < n && i < 5; i++) {
        int bars = aps[i].rssi > -55 ? 4 : aps[i].rssi > -67 ? 3 : aps[i].rssi > -78 ? 2 : 1;
        printf("   %-24.24s ch %2d  %4d dBm  %.*s\n", aps[i].ssid[0] ? (char *)aps[i].ssid : "(hidden)",
               aps[i].primary, aps[i].rssi, bars, "||||");
    }
    free(aps);
    // radio stays on (its tasks show up in the task list below) and joins the workshop network next
}
#endif

/* ---------------------------------------------------------------- tasks */
static void report_tasks(void)
{
    UBaseType_t n = uxTaskGetNumberOfTasks();
    TaskStatus_t *tasks = calloc(n, sizeof(TaskStatus_t));
    if (!tasks) {
        return;
    }
    n = uxTaskGetSystemState(tasks, n, NULL);
    title("TASKS RUNNING RIGHT NOW (FreeRTOS)");
    for (UBaseType_t i = 0; i < n; i++) {
        BaseType_t core = xTaskGetCoreID(tasks[i].xHandle);
        printf(" %-14s priority %2u   CPU %s\n", tasks[i].pcTaskName, (unsigned)tasks[i].uxCurrentPriority,
               core == tskNO_AFFINITY ? "any" : (core == 0 ? "0" : "1"));
    }
    free(tasks);
}

/* ---------------------------------------------------------------- RAM vs flash */
static void report_counters(void)
{
    static uint32_t boots_ram;   // normal RAM: starts at 0 after every reset
    boots_ram++;

    uint32_t boots_flash = 0;
    nvs_handle_t h;
    if (nvs_open("hello", NVS_READWRITE, &h) == ESP_OK) {
        nvs_get_u32(h, "boots", &boots_flash);
        boots_flash++;
        nvs_set_u32(h, "boots", boots_flash);
        nvs_commit(h);
        nvs_close(h);
    }

    title("RESTART COUNTERS: which memory remembers?");
    printf(" Counter in RAM   : %" PRIu32 "   (wiped on every reset)\n", boots_ram);
    printf(" Counter in flash : %" PRIu32 "   (kept, even with power off)\n", boots_flash);
    printf(" Press BOOT to restart, or unplug and plug back in, and compare.\n");
}

void board_report(board_info_t *info)
{
    s_info = info;
    memset(info, 0, sizeof(*info));
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    printf("\n\n  ============  HELLO FROM YOUR %s BOARD  ============\n", CONFIG_IDF_TARGET);
    report_chip();
    report_memory();
    report_partitions();
#if CONFIG_PROBE_CAMERA
    report_camera();
#endif
#if CONFIG_PROBE_MIC
    report_mic();
#else
    title("MICROPHONE");
    printf(" Skipped    (mic pins not set for this board: menuconfig -> Hello Board)\n");
#endif
#if CONFIG_PROBE_WIFI
    report_wifi();
#endif
    report_tasks();
    report_counters();
    printf("------------------------------------------------------------\n\n");
    (void)counter_in_ram;
}
