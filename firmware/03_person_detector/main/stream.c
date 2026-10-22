/*
 * Live view of what the model sees.
 *   http://<board-ip>/stream     MJPEG (open it in a browser on a good network)
 *   http://<board-ip>/frame.jpg  single picture
 *   UDP video (used by the dashboard): anyone who sends "watch" to UDP port 5006 gets
 *   every new frame pushed back to them as one UDP packet for the next 5 seconds:
 *     "WSF1" | frame number (uint32, big-endian) | JPEG bytes
 *   UDP survives lossy Wi-Fi (e.g. phone hotspots) far better than a TCP stream:
 *   a lost packet is just a skipped frame, instead of a stalled connection.
 * The 96x96 grayscale frame is JPEG-encoded in software only while someone is watching.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_http_server.h"
#include "esp_heap_caps.h"
#include "lwip/sockets.h"
#include "esp_log.h"
#include "img_converters.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "app.h"

static const char *TAG = "stream";

#define BOUNDARY "workshopframe"
#define STREAM_MAX_FPS 10
#define JPEG_QUALITY 80
#define VIDEO_UDP_PORT 5006
#define WATCH_TIMEOUT_US (5 * 1000 * 1000)

/* Frame buffers live in PSRAM when available: internal RAM is reserved for the model + Wi-Fi */
static uint8_t *frame_buf_alloc(void)
{
    return heap_caps_malloc_prefer(FRAME_W * FRAME_H, 2, MALLOC_CAP_SPIRAM, MALLOC_CAP_8BIT);
}

static uint8_t *s_frame;   // used by the HTTP handlers (one at a time)

static esp_err_t encode_latest(uint8_t **jpg, size_t *len, uint32_t *seq)
{
    *seq = frame_copy(s_frame);
    if (!fmt2jpg(s_frame, FRAME_W * FRAME_H, FRAME_W, FRAME_H, PIXFORMAT_GRAYSCALE, JPEG_QUALITY, jpg, len)) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t frame_handler(httpd_req_t *req)
{
    uint8_t *jpg = NULL;
    size_t len = 0;
    uint32_t seq;
    if (encode_latest(&jpg, &len, &seq) != ESP_OK) {
        return httpd_resp_send_500(req);
    }
    httpd_resp_set_type(req, "image/jpeg");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    esp_err_t err = httpd_resp_send(req, (const char *)jpg, len);
    free(jpg);
    return err;
}

static esp_err_t stream_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "multipart/x-mixed-replace;boundary=" BOUNDARY);
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    ESP_LOGI(TAG, "viewer connected");

    // Send each frame immediately instead of letting TCP wait to batch small packets
    int fd = httpd_req_to_sockfd(req);
    int one = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

    uint32_t last_seq = 0;
    char head[96];
    uint8_t *out = NULL;
    size_t out_cap = 0;
    while (1) {
        uint32_t seq = frame_copy(s_frame);
        if (seq == last_seq) {   // no new frame yet
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        last_seq = seq;
        uint8_t *jpg = NULL;
        size_t len = 0;
        if (!fmt2jpg(s_frame, FRAME_W * FRAME_H, FRAME_W, FRAME_H, PIXFORMAT_GRAYSCALE, JPEG_QUALITY, &jpg, &len)) {
            break;
        }
        int n = snprintf(head, sizeof(head), "\r\n--" BOUNDARY "\r\nContent-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n",
                         (unsigned)len);
        // header + image in one write = one TCP segment burst per frame
        if (out_cap < n + len) {
            free(out);
            out_cap = n + len + 1024;
            out = malloc(out_cap);
        }
        esp_err_t err = ESP_ERR_NO_MEM;
        if (out) {
            memcpy(out, head, n);
            memcpy(out + n, jpg, len);
            err = httpd_resp_send_chunk(req, (const char *)out, n + len);
        }
        free(jpg);
        if (err != ESP_OK) {
            break;   // viewer went away
        }
        vTaskDelay(pdMS_TO_TICKS(1000 / STREAM_MAX_FPS));
    }
    free(out);
    ESP_LOGI(TAG, "viewer left");
    return ESP_OK;
}

static void udp_video_task(void *arg)
{
    uint8_t *s_udp_frame = frame_buf_alloc();
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    struct sockaddr_in me = { .sin_family = AF_INET, .sin_port = htons(VIDEO_UDP_PORT), .sin_addr.s_addr = htonl(INADDR_ANY) };
    if (sock < 0 || bind(sock, (struct sockaddr *)&me, sizeof(me)) < 0) {
        ESP_LOGE(TAG, "UDP video socket failed");
        vTaskDelete(NULL);
    }
    struct timeval tv = { .tv_sec = 0, .tv_usec = 20 * 1000 };
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    struct sockaddr_in viewer = { 0 };
    int64_t viewer_until = 0;
    uint32_t last_seq = 0;
    int64_t last_sent = 0;
    char req[16];

    while (1) {
        // 1. any (new) viewer asking for frames?
        struct sockaddr_in from;
        socklen_t flen = sizeof(from);
        int n = recvfrom(sock, req, sizeof(req), 0, (struct sockaddr *)&from, &flen);
        if (n >= 5 && memcmp(req, "watch", 5) == 0) {
            if (viewer_until < esp_timer_get_time()) {
                char ip[16];
                inet_ntoa_r(from.sin_addr, ip, sizeof(ip));
                ESP_LOGI(TAG, "UDP viewer %s:%d", ip, ntohs(from.sin_port));
            }
            viewer = from;
            viewer_until = esp_timer_get_time() + WATCH_TIMEOUT_US;
        }

        // 2. push the newest frame to the viewer, at most STREAM_MAX_FPS
        int64_t now = esp_timer_get_time();
        if (now > viewer_until || now - last_sent < 1000000 / STREAM_MAX_FPS) {
            continue;
        }
        uint32_t seq = frame_copy(s_udp_frame);
        if (seq == last_seq) {
            continue;
        }
        uint8_t *jpg = NULL;
        size_t len = 0;
        if (!fmt2jpg(s_udp_frame, FRAME_W * FRAME_H, FRAME_W, FRAME_H, PIXFORMAT_GRAYSCALE, JPEG_QUALITY, &jpg, &len)) {
            continue;
        }
        if (len + 8 <= 1400) {   // must fit in one Wi-Fi packet
            uint8_t *pkt = malloc(len + 8);
            if (pkt) {
                memcpy(pkt, "WSF1", 4);
                pkt[4] = seq >> 24; pkt[5] = seq >> 16; pkt[6] = seq >> 8; pkt[7] = seq;
                memcpy(pkt + 8, jpg, len);
                sendto(sock, pkt, len + 8, 0, (struct sockaddr *)&viewer, sizeof(viewer));
                free(pkt);
            }
        }
        free(jpg);
        last_seq = seq;
        last_sent = now;
    }
}

void stream_start(void)
{
    s_frame = frame_buf_alloc();
    xTaskCreatePinnedToCore(udp_video_task, "udp_video", 6144, NULL, 3, NULL, 0);

    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.core_id = 0;
    cfg.send_wait_timeout = 3;
    cfg.max_open_sockets = 4;
    httpd_handle_t server = NULL;
    if (httpd_start(&server, &cfg) != ESP_OK) {
        ESP_LOGE(TAG, "HTTP server failed to start");
        return;
    }
    const httpd_uri_t uris[] = {
        { .uri = "/stream", .method = HTTP_GET, .handler = stream_handler },
        { .uri = "/frame.jpg", .method = HTTP_GET, .handler = frame_handler },
    };
    for (size_t i = 0; i < sizeof(uris) / sizeof(uris[0]); i++) {
        httpd_register_uri_handler(server, &uris[i]);
    }
    ESP_LOGI(TAG, "Live view: http://<board-ip>/stream");
}
