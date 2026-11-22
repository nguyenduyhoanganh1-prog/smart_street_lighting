/**
 * @file web_server.c
 * @brief HTTP server of ai_stream (see web_server.h).
 *
 * All handlers run in the single httpd task, so the static buffers need no lock.
 */
#include "web_server.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "ai_pipeline.h"
#include "app_config.h"
#include "cJSON.h"
#include "detector.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "lamp_ctrl.h"
#include "tune_config.h"

#define TAG "web"

#define STATE_BUF_SIZE    512U
#define ROAD_BUF_SIZE     512U   /* road + left end + 2 kerb lines + bounds, ~300 chars at most */
#define ROAD_LEFT_CAP     48U
#define REPLY_BUF_SIZE    192U
#define HDR_INFO_CAP      96U
#define HDR_DET_CAP       320U   /* 10 objects x at most 22 chars + separators */
#define HTTPD_STACK_SIZE  8192U
#define MAX_URI_HANDLERS  8U
#define MAX_OPEN_SOCKETS  5U
#define PER_MILLE         1000.0f

extern const char index_html_start[] asm("_binary_index_html_start");
extern const char index_html_end[] asm("_binary_index_html_end");

static char s_state_buf[STATE_BUF_SIZE];
static char s_road_buf[ROAD_BUF_SIZE];
static char s_reply_buf[REPLY_BUF_SIZE];
static char s_body_buf[TUNE_HTTP_MAX_BODY + 1U];
static char s_hdr_info[HDR_INFO_CAP];
static char s_hdr_det[HDR_DET_CAP];
static uint8_t s_jpeg_out[APP_STREAM_JPEG_BYTES];

/* ================================ Request helpers ================================ */

static esp_err_t reply_json(httpd_req_t *req, const char *status, const char *json)
{
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, json);
}

static esp_err_t reply_error(httpd_req_t *req, const char *status, const char *message)
{
    const int n = snprintf(s_reply_buf, sizeof(s_reply_buf), "{\"ok\":false,\"error\":\"%s\"}", message);
    return reply_json(req, status, (n > 0 && (size_t)n < sizeof(s_reply_buf)) ? s_reply_buf : "{\"ok\":false}");
}

/** Parse the request body as a JSON object. The caller frees it with cJSON_Delete(). */
static cJSON *read_json_body(httpd_req_t *req)
{
    if (req->content_len == 0U || req->content_len > TUNE_HTTP_MAX_BODY) {
        return NULL;
    }
    size_t got = 0;
    while (got < req->content_len) {
        const int r = httpd_req_recv(req, s_body_buf + got, req->content_len - got);
        if (r == HTTPD_SOCK_ERR_TIMEOUT) {
            continue;
        }
        if (r <= 0) {
            return NULL;
        }
        got += (size_t)r;
    }
    cJSON *root = cJSON_ParseWithLength(s_body_buf, got);
    if (root != NULL && !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return NULL;
    }
    return root;
}

static bool json_get_bool(const cJSON *root, const char *key, bool *out)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(root, key);
    if (!cJSON_IsBool(item)) {
        return false;
    }
    *out = cJSON_IsTrue(item);
    return true;
}

/** Whole number in 0..max. */
static bool json_get_uint(const cJSON *root, const char *key, uint32_t max, uint32_t *out)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(root, key);
    if (!cJSON_IsNumber(item) || item->valuedouble < 0.0 || item->valuedouble > (double)max
        || item->valuedouble != floor(item->valuedouble)) {
        return false;
    }
    *out = (uint32_t)item->valuedouble;
    return true;
}

static unsigned per_mille(float value)
{
    if (value <= 0.0f) {
        return 0U;
    }
    return value >= 1.0f ? (unsigned)PER_MILLE : (unsigned)(value * PER_MILLE + 0.5f);
}

/* ================================== Handlers ================================== */

static esp_err_t index_get(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    /* The page is embedded with a trailing NUL: do not send it */
    return httpd_resp_send(req, index_html_start, (ssize_t)(index_html_end - index_html_start - 1));
}

/** "name:score%:cx:cy:w:h|..." with positions in per mille of the frame (see index.html) */
static void encode_detections(const detection_result_t *det, char *buf, size_t cap)
{
    size_t n = 0;
    buf[0] = '\0';
    for (uint32_t i = 0; i < det->count; i++) {
        const detection_t *d = &det->items[i];
        const int w = snprintf(buf + n, cap - n, "%s%s:%u:%u:%u:%u:%u", i > 0U ? "|" : "", detector_label_name(d->label),
                               per_mille(d->score) / 10U, per_mille(d->cx), per_mille(d->cy), per_mille(d->w),
                               per_mille(d->h));
        if (w < 0 || (size_t)w >= cap - n) {
            buf[n] = '\0'; /* does not fit: drop this object and the rest */
            return;
        }
        n += (size_t)w;
    }
}

static esp_err_t frame_get(httpd_req_t *req)
{
    ai_snapshot_t meta;
    const size_t len = ai_pipeline_get_jpeg(s_jpeg_out, sizeof(s_jpeg_out), &meta);
    if (len == 0U) {
        return reply_error(req, "503 Service Unavailable", "no frame yet");
    }
    encode_detections(&meta.det, s_hdr_det, sizeof(s_hdr_det));
    snprintf(s_hdr_info, sizeof(s_hdr_info), "%u,%u,%u,%u,%u,%u", (unsigned)meta.seq, (unsigned)meta.det.resize_us,
             (unsigned)meta.det.dsp_us, (unsigned)meta.det.infer_us, (unsigned)meta.loop_ms,
             (unsigned)meta.jpeg_bytes);
    httpd_resp_set_type(req, "image/jpeg");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(req, "X-Info", s_hdr_info); /* seq,resize_us,dsp_us,infer_us,loop_ms,jpeg_bytes */
    httpd_resp_set_hdr(req, "X-Det", s_hdr_det);
    return httpd_resp_send(req, (const char *)s_jpeg_out, (ssize_t)len);
}

static esp_err_t state_get(httpd_req_t *req)
{
    uint8_t pct[APP_LAMP_COUNT];
    bool auto_on = false;
    lamp_ctrl_get(pct, &auto_on);
    ai_snapshot_t meta;
    ai_pipeline_get_snapshot(&meta);

    const int n = snprintf(s_state_buf, sizeof(s_state_buf),
                           "{\"lamps\":[%u,%u,%u,%u],\"auto\":%s,\"locked\":%s,\"seq\":%u,\"loop_ms\":%u,"
                           "\"infer_us\":%u,\"heap_int\":%u,\"heap_psram\":%u}",
                           (unsigned)pct[0], (unsigned)pct[1], (unsigned)pct[2], (unsigned)pct[3],
                           auto_on ? "true" : "false", meta.exposure_locked ? "true" : "false", (unsigned)meta.seq,
                           (unsigned)meta.loop_ms, (unsigned)meta.det.infer_us,
                           (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                           (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    if (n <= 0 || (size_t)n >= sizeof(s_state_buf)) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "state too large");
    }
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, s_state_buf, n);
}

/** Start-up road calibration for the page: road edges, left end and zone boundaries, in frame pixels. */
static esp_err_t road_get(httpd_req_t *req)
{
    road_zones_t road;
    const ai_road_state_t state = ai_pipeline_get_road(&road);
    int n;
    if (state != AI_ROAD_FOUND) {
        n = snprintf(s_road_buf, sizeof(s_road_buf), "{\"state\":\"%s\",\"w\":%u,\"h\":%u}",
                     state == AI_ROAD_PENDING ? "pending" : "not_found", (unsigned)APP_FRAME_WIDTH,
                     (unsigned)APP_FRAME_HEIGHT);
    } else {
        char left[ROAD_LEFT_CAP] = "null";
        char kerb_top[ROAD_LEFT_CAP] = "null";    /* outer kerb edges, null when that side was not found */
        char kerb_bottom[ROAD_LEFT_CAP] = "null";
        if (road.has_left) {
            snprintf(left, sizeof(left), "[%.5f,%.3f]", (double)road.left_a, (double)road.left_b);
        }
        if (road.has_kerb_top) {
            snprintf(kerb_top, sizeof(kerb_top), "[%.5f,%.3f]", (double)road.kerb_top_a, (double)road.kerb_top_b);
        }
        if (road.has_kerb_bottom) {
            snprintf(kerb_bottom, sizeof(kerb_bottom), "[%.5f,%.3f]", (double)road.kerb_bottom_a,
                     (double)road.kerb_bottom_b);
        }
        n = snprintf(s_road_buf, sizeof(s_road_buf),
                     "{\"state\":\"found\",\"w\":%u,\"h\":%u,\"top\":[%.5f,%.3f],\"bottom\":[%.5f,%.3f],"
                     "\"left\":%s,\"kerb_top\":%s,\"kerb_bottom\":%s,\"x_min\":%d,\"x_max\":%d,"
                     "\"bounds\":[%.2f,%.2f,%.2f]}",
                     (unsigned)APP_FRAME_WIDTH, (unsigned)APP_FRAME_HEIGHT, (double)road.top_a, (double)road.top_b,
                     (double)road.bottom_a, (double)road.bottom_b, left, kerb_top, kerb_bottom, road.x_min,
                     road.x_max, (double)road.boundaries_x[0], (double)road.boundaries_x[1],
                     (double)road.boundaries_x[2]);
    }
    if (n <= 0 || (size_t)n >= sizeof(s_road_buf)) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "road too large");
    }
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, s_road_buf, n);
}

static esp_err_t lamp_post(httpd_req_t *req)
{
    cJSON *root = read_json_body(req);
    if (root == NULL) {
        return reply_error(req, "400 Bad Request", "Yêu cầu không hợp lệ");
    }
    uint32_t lamp = 0;
    uint32_t pct = 0;
    const bool valid = json_get_uint(root, "lamp", APP_LAMP_COUNT, &lamp) && json_get_uint(root, "pct", 100U, &pct);
    cJSON_Delete(root);
    if (!valid || lamp_ctrl_set_manual(lamp, (uint8_t)pct) != ESP_OK) {
        return reply_error(req, "400 Bad Request", "Đèn từ 0 (tất cả) đến 4, độ sáng từ 0 đến 100");
    }
    return reply_json(req, "200 OK", "{\"ok\":true}");
}

static esp_err_t auto_post(httpd_req_t *req)
{
    cJSON *root = read_json_body(req);
    bool on = false;
    const bool valid = root != NULL && json_get_bool(root, "on", &on);
    cJSON_Delete(root);
    if (!valid) {
        return reply_error(req, "400 Bad Request", "Thiếu trường on");
    }
    lamp_ctrl_set_auto(on);
    return reply_json(req, "200 OK", "{\"ok\":true}");
}

static esp_err_t exposure_post(httpd_req_t *req)
{
    cJSON *root = read_json_body(req);
    bool lock = false;
    const bool valid = root != NULL && json_get_bool(root, "lock", &lock);
    cJSON_Delete(root);
    if (!valid) {
        return reply_error(req, "400 Bad Request", "Thiếu trường lock");
    }
    if (ai_pipeline_set_exposure_lock(lock) != ESP_OK) {
        return reply_error(req, "500 Internal Server Error", "Không đổi được chế độ phơi sáng");
    }
    return reply_json(req, "200 OK", "{\"ok\":true}");
}

static esp_err_t recalibrate_post(httpd_req_t *req)
{
    ai_pipeline_request_recalibration(); /* no body needed; the page then polls /api/road */
    return reply_json(req, "200 OK", "{\"ok\":true}");
}

/* ================================== Start ================================== */

esp_err_t web_server_start(void)
{
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.stack_size = HTTPD_STACK_SIZE;
    cfg.max_uri_handlers = MAX_URI_HANDLERS;
    cfg.max_open_sockets = MAX_OPEN_SOCKETS;
    cfg.lru_purge_enable = true; /* a phone that vanished must not block the next one */

    httpd_handle_t server = NULL;
    esp_err_t err = httpd_start(&server, &cfg);
    if (err != ESP_OK) {
        return err;
    }
    const httpd_uri_t routes[] = {
        { .uri = "/", .method = HTTP_GET, .handler = index_get },
        { .uri = "/frame.jpg", .method = HTTP_GET, .handler = frame_get },
        { .uri = "/api/state", .method = HTTP_GET, .handler = state_get },
        { .uri = "/api/road", .method = HTTP_GET, .handler = road_get },
        { .uri = "/api/lamp", .method = HTTP_POST, .handler = lamp_post },
        { .uri = "/api/auto", .method = HTTP_POST, .handler = auto_post },
        { .uri = "/api/exposure", .method = HTTP_POST, .handler = exposure_post },
        { .uri = "/api/recalibrate", .method = HTTP_POST, .handler = recalibrate_post },
    };
    _Static_assert(sizeof(routes) / sizeof(routes[0]) <= MAX_URI_HANDLERS, "raise MAX_URI_HANDLERS");
    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); i++) {
        err = httpd_register_uri_handler(server, &routes[i]);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "register %s failed: %s", routes[i].uri, esp_err_to_name(err));
            return err;
        }
    }
    ESP_LOGI(TAG, "web server ready on port %d", cfg.server_port);
    return ESP_OK;
}
