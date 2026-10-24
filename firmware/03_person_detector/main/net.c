/*
 * Wi-Fi station + UDP broadcast reporter.
 *
 * The board never needs to know the dashboard's IP: it sends to the
 * network's broadcast address (e.g. 192.168.1.255) and whoever listens on
 * the port gets every team's packets.
 */
#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_mac.h"
#include "esp_log.h"
#include "esp_psram.h"
#include "esp_timer.h"
#include "lwip/sockets.h"
#include "sdkconfig.h"
#include "app.h"

static const char *TAG = "net";

#define GOT_IP_BIT BIT0

static EventGroupHandle_t s_events;
static esp_netif_t *s_netif;
static TaskHandle_t s_reporter;

static void on_wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(s_events, GOT_IP_BIT);
        ESP_LOGW(TAG, "Wi-Fi \"%s\" not connected, retrying...", g_config.ssid);
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *e = (ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "Connected! IP " IPSTR " - sending to dashboard on UDP %d",
                 IP2STR(&e->ip_info.ip), CONFIG_WORKSHOP_UDP_PORT);
        xEventGroupSetBits(s_events, GOT_IP_BIT);
        net_notify();
    }
}

static int build_packet(char *buf, size_t cap, const esp_netif_ip_info_t *ip)
{
    detect_state_t st;
    state_get(&st);
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    wifi_ap_record_t ap;
    int rssi = (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) ? ap.rssi : 0;

    // team name is restricted to [A-Za-z0-9 _-.] by config_team_valid(), so it is JSON-safe
    return snprintf(buf, cap,
                    "{\"v\":1,\"fw\":\"" FW_VERSION "\",\"team\":\"%s\",\"mac\":\"%02x:%02x:%02x:%02x:%02x:%02x\","
                    "\"ip\":\"" IPSTR "\",\"person\":%s,\"score\":%d,\"seen\":%lu,"
                    "\"infer_ms\":%lu,\"uptime_s\":%lu,\"chip\":\"%s\",\"psram\":%s,\"rssi\":%d,\"fps\":%lu.%lu,\"cam\":true}",
                    g_config.team, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
                    IP2STR(&ip->ip), st.person ? "true" : "false", st.score,
                    (unsigned long)st.seen, (unsigned long)st.infer_ms,
                    (unsigned long)(esp_timer_get_time() / 1000000), CONFIG_IDF_TARGET,
                    esp_psram_is_initialized() ? "true" : "false", rssi,
                    (unsigned long)(st.fps_x10 / 10), (unsigned long)(st.fps_x10 % 10));
}

static void reporter_task(void *arg)
{
    int sock = -1;
    char buf[320];
    while (1) {
        // wake up every second, or immediately when net_notify() is called
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1000));
        xEventGroupWaitBits(s_events, GOT_IP_BIT, pdFALSE, pdTRUE, portMAX_DELAY);

        if (sock < 0) {
            sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
            int yes = 1;
            setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &yes, sizeof(yes));
        }

        esp_netif_ip_info_t ip;
        if (esp_netif_get_ip_info(s_netif, &ip) != ESP_OK || ip.ip.addr == 0) {
            continue;
        }
        struct sockaddr_in dest = {
            .sin_family = AF_INET,
            .sin_port = htons(CONFIG_WORKSHOP_UDP_PORT),
            .sin_addr.s_addr = ip.ip.addr | ~ip.netmask.addr,   // subnet broadcast address
        };
        int len = build_packet(buf, sizeof(buf), &ip);
        if (sendto(sock, buf, len, 0, (struct sockaddr *)&dest, sizeof(dest)) < 0) {
            ESP_LOGW(TAG, "sendto failed (errno %d)", errno);
            close(sock);
            sock = -1;
        }
    }
}

void net_notify(void)
{
    if (s_reporter) {
        xTaskNotifyGive(s_reporter);
    }
}

void net_start(void)
{
    s_events = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    s_netif = esp_netif_create_default_wifi_sta();

    // Show the team name as the hostname in the router's client list
    char host[TEAM_MAX_LEN + 1];
    size_t j = 0;
    for (size_t i = 0; g_config.team[i] && j < sizeof(host) - 1; i++) {
        char c = g_config.team[i];
        host[j++] = (c == ' ' || c == '_' || c == '.') ? '-' : c;
    }
    host[j] = '\0';
    esp_netif_set_hostname(s_netif, host);

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_wifi_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_wifi_event, NULL));

    wifi_config_t wc = { 0 };
    strlcpy((char *)wc.sta.ssid, g_config.ssid, sizeof(wc.sta.ssid));
    strlcpy((char *)wc.sta.password, g_config.pass, sizeof(wc.sta.password));
    wc.sta.threshold.authmode = g_config.pass[0] ? WIFI_AUTH_WPA_PSK : WIFI_AUTH_OPEN;
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wc));
    ESP_ERROR_CHECK(esp_wifi_start());
    esp_wifi_set_ps(WIFI_PS_NONE);

    xTaskCreatePinnedToCore(reporter_task, "reporter", 4096, NULL, 4, &s_reporter, 0);
    ESP_LOGI(TAG, "Joining Wi-Fi \"%s\"...", g_config.ssid);
}
