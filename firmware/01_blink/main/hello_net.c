/*
 * Join the workshop Wi-Fi and say hello to the dashboard.
 *
 * Every 2 s the board broadcasts one small JSON packet (team name + what the
 * board report found) to the whole network. The dashboard on the projector
 * listens on that port, so everyone can see whose board is online, which
 * camera it has, and whether the team name is set.
 */
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_mac.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs.h"
#include "lwip/sockets.h"
#include "sdkconfig.h"
#include "hello_net.h"

static const char *TAG = "net";
static const char *NS = "workshop";

static char s_team[TEAM_MAX_LEN + 1];
static char s_ssid[33];
static char s_pass[65];
static board_info_t s_info;
static esp_netif_t *s_netif;
static volatile bool s_connected;
static TaskHandle_t s_reporter;

/* ---------------------------------------------------------------- settings */

/* Team names travel in JSON and appear on the projector: keep them simple. */
static bool team_valid(const char *team)
{
    size_t n = strlen(team);
    if (n == 0 || n > TEAM_MAX_LEN) {
        return false;
    }
    for (size_t i = 0; i < n; i++) {
        char c = team[i];
        if (!(isalnum((unsigned char)c) || c == ' ' || c == '-' || c == '_' || c == '.')) {
            return false;
        }
    }
    return true;
}

static void read_str(nvs_handle_t h, const char *key, char *dst, size_t cap)
{
    size_t len = cap;
    if (nvs_get_str(h, key, dst, &len) != ESP_OK) {
        dst[0] = '\0';
    }
}

static void settings_load(void)
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) == ESP_OK) {
        read_str(h, "team", s_team, sizeof(s_team));
        read_str(h, "ssid", s_ssid, sizeof(s_ssid));
        read_str(h, "pass", s_pass, sizeof(s_pass));
        nvs_close(h);
    }
    if (!team_valid(s_team)) {
        s_team[0] = '\0';   // not set yet: the dashboard shows "no team name"
    }
    if (s_ssid[0] == '\0') {
        strlcpy(s_ssid, CONFIG_WORKSHOP_WIFI_SSID, sizeof(s_ssid));
        strlcpy(s_pass, CONFIG_WORKSHOP_WIFI_PASSWORD, sizeof(s_pass));
    }
}

static esp_err_t write_strs(const char *k1, const char *v1, const char *k2, const char *v2)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NS, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_str(h, k1, v1);
    if (err == ESP_OK && k2) {
        err = nvs_set_str(h, k2, v2);
    }
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    return err;
}

const char *team_get(void)
{
    return s_team;
}

esp_err_t team_set(const char *team)
{
    if (!team_valid(team)) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = write_strs("team", team, NULL, NULL);
    if (err == ESP_OK) {
        strlcpy(s_team, team, sizeof(s_team));
        if (s_reporter) {
            xTaskNotifyGive(s_reporter);   // show the new name right away
        }
    }
    return err;
}

esp_err_t wifi_settings_set(const char *ssid, const char *pass)
{
    if (strlen(ssid) == 0 || strlen(ssid) > 32 || strlen(pass) > 64) {
        return ESP_ERR_INVALID_ARG;
    }
    return write_strs("ssid", ssid, "pass", pass);
}

/* ---------------------------------------------------------------- Wi-Fi */

esp_err_t wifi_start_once(void)
{
    if (s_netif) {
        return ESP_OK;
    }
    esp_netif_init();
    esp_event_loop_create_default();
    s_netif = esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t err = esp_wifi_init(&cfg);
    if (err == ESP_OK) {
        err = esp_wifi_set_mode(WIFI_MODE_STA);
    }
    if (err == ESP_OK) {
        err = esp_wifi_start();
    }
    return err;
}

static void on_wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_connected) {
            ESP_LOGW(TAG, "Lost Wi-Fi \"%s\", reconnecting...", s_ssid);
        }
        s_connected = false;
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *e = (ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "Connected to \"%s\" as " IPSTR ": you're on the big screen now", s_ssid, IP2STR(&e->ip_info.ip));
        s_connected = true;
        xTaskNotifyGive(s_reporter);
    }
}

/* ---------------------------------------------------------------- dashboard packets */

static int build_packet(char *buf, size_t cap, const esp_netif_ip_info_t *ip)
{
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    wifi_ap_record_t ap;
    int rssi = (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) ? ap.rssi : 0;

    // team and camera names only contain [A-Za-z0-9 _-.], so they are JSON-safe
    return snprintf(buf, cap,
                    "{\"v\":1,\"fw\":\"" FW_VERSION "\",\"app\":\"hello\",\"team\":\"%s\",\"mac\":\"%02x:%02x:%02x:%02x:%02x:%02x\","
                    "\"ip\":\"" IPSTR "\",\"uptime_s\":%lu,\"chip\":\"%s\",\"rssi\":%d,"
                    "\"psram\":%s,\"psram_mb\":%u,\"flash_mb\":%u,\"camera\":\"%s\",\"mic\":%s}",
                    s_team, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], IP2STR(&ip->ip),
                    (unsigned long)(esp_timer_get_time() / 1000000), CONFIG_IDF_TARGET, rssi,
                    s_info.psram_mb ? "true" : "false", s_info.psram_mb, s_info.flash_mb,
                    s_info.camera, s_info.mic ? "true" : "false");
}

static void reporter_task(void *arg)
{
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    int yes = 1;
    setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &yes, sizeof(yes));
    char buf[320];
    while (1) {
        // every 2 s, or right away when the team name changes
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(2000));
        esp_netif_ip_info_t ip;
        if (!s_connected || esp_netif_get_ip_info(s_netif, &ip) != ESP_OK || ip.ip.addr == 0) {
            continue;
        }
        struct sockaddr_in dest = {
            .sin_family = AF_INET,
            .sin_port = htons(CONFIG_WORKSHOP_UDP_PORT),
            .sin_addr.s_addr = ip.ip.addr | ~ip.netmask.addr,   // subnet broadcast address
        };
        int len = build_packet(buf, sizeof(buf), &ip);
        sendto(sock, buf, len, 0, (struct sockaddr *)&dest, sizeof(dest));
    }
}

void hello_net_start(const board_info_t *info)
{
    s_info = *info;
    settings_load();
    if (wifi_start_once() != ESP_OK) {
        ESP_LOGE(TAG, "Wi-Fi did not start");
        return;
    }
    if (s_team[0]) {
        esp_netif_set_hostname(s_netif, s_team);   // shows up in the hotspot's client list
    }
    xTaskCreatePinnedToCore(reporter_task, "reporter", 4096, NULL, 4, &s_reporter, 0);
    esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, on_wifi_event, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_wifi_event, NULL);

    wifi_config_t wc = { 0 };
    strlcpy((char *)wc.sta.ssid, s_ssid, sizeof(wc.sta.ssid));
    strlcpy((char *)wc.sta.password, s_pass, sizeof(wc.sta.password));
    wc.sta.threshold.authmode = s_pass[0] ? WIFI_AUTH_WPA_PSK : WIFI_AUTH_OPEN;
    esp_wifi_set_config(WIFI_IF_STA, &wc);
    esp_wifi_set_ps(WIFI_PS_NONE);
    esp_wifi_connect();
    ESP_LOGI(TAG, "Joining Wi-Fi \"%s\"...", s_ssid);
}
