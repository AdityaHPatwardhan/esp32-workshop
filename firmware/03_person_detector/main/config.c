#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_mac.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include "app.h"

static const char *TAG = "config";
static const char *NS = "workshop";

app_config_t g_config;

/* Team names travel in JSON and appear on the projector: keep them simple. */
bool config_team_valid(const char *team)
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

void config_load(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    memset(&g_config, 0, sizeof(g_config));
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) == ESP_OK) {
        read_str(h, "team", g_config.team, sizeof(g_config.team));
        read_str(h, "ssid", g_config.ssid, sizeof(g_config.ssid));
        read_str(h, "pass", g_config.pass, sizeof(g_config.pass));
        nvs_close(h);
    }

    if (!config_team_valid(g_config.team)) {
        uint8_t mac[6];
        esp_read_mac(mac, ESP_MAC_WIFI_STA);
        snprintf(g_config.team, sizeof(g_config.team), "team-%02x%02x", mac[4], mac[5]);
    }
    if (g_config.ssid[0] == '\0') {
        strlcpy(g_config.ssid, CONFIG_WORKSHOP_WIFI_SSID, sizeof(g_config.ssid));
        strlcpy(g_config.pass, CONFIG_WORKSHOP_WIFI_PASSWORD, sizeof(g_config.pass));
    }
    ESP_LOGI(TAG, "team=\"%s\" wifi=\"%s\"", g_config.team, g_config.ssid);
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

esp_err_t config_set_team(const char *team)
{
    if (!config_team_valid(team)) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = write_strs("team", team, NULL, NULL);
    if (err == ESP_OK) {
        strlcpy(g_config.team, team, sizeof(g_config.team));
    }
    return err;
}

esp_err_t config_set_wifi(const char *ssid, const char *pass)
{
    if (strlen(ssid) == 0 || strlen(ssid) > 32 || strlen(pass) > 64) {
        return ESP_ERR_INVALID_ARG;
    }
    return write_strs("ssid", ssid, "pass", pass);
}
