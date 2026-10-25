/**
 * @file web_server.c
 * @brief HTTP server of thu_dataset (see web_server.h).
 *
 * All handlers run in the single httpd task, so the static buffers need no lock. The preview is
 * a plain JPEG that the page re-requests, not a never-ending stream: a stream loop would keep
 * the only server task busy and block every other route.
 */
#include "web_server.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_config.h"
#include "cJSON.h"
#include "camera_cap.h"
#include "capture_ctrl.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "gray_image.h"
#include "img_converters.h"
#include "tune_config.h"

#define TAG "web"

#define STATUS_BUF_SIZE     1024U
#define REPLY_BUF_SIZE      192U
#define PREVIEW_JPEG_BYTES  (16U * 1024U) /* a 160x90 gray JPEG is a few KB; this is generous */
#define PREVIEW_SCALE       (APP_FRAME_WIDTH / APP_PREVIEW_WIDTH)
#define PREVIEW_LOCK_MS     300U
#define HTTPD_STACK_SIZE    8192U
#define MAX_URI_HANDLERS    12U
#define MAX_OPEN_SOCKETS    7U

extern const char index_html_start[] asm("_binary_index_html_start");
extern const char index_html_end[] asm("_binary_index_html_end");

typedef struct {
    uint8_t *buf;
    size_t cap;
    size_t len;
} jpeg_sink_t;

_Static_assert(APP_FRAME_WIDTH % APP_PREVIEW_WIDTH == 0U && APP_FRAME_HEIGHT % APP_PREVIEW_HEIGHT == 0U
                   && APP_FRAME_WIDTH / APP_PREVIEW_WIDTH == APP_FRAME_HEIGHT / APP_PREVIEW_HEIGHT,
               "the preview must be the stored frame divided by one whole factor");

static char s_status_buf[STATUS_BUF_SIZE];
static char s_reply_buf[REPLY_BUF_SIZE];
static char s_body_buf[TUNE_HTTP_MAX_BODY + 1U];
/* The preview is tiny (160x90 gray = 14400 bytes), so its buffers are plain static memory */
static uint8_t s_preview_gray[APP_PREVIEW_WIDTH * APP_PREVIEW_HEIGHT];
static uint8_t s_preview_jpeg[PREVIEW_JPEG_BYTES];
static jpeg_sink_t s_preview = { .buf = s_preview_jpeg, .cap = sizeof(s_preview_jpeg), .len = 0U };

/* ================================== JSON ================================== */

/* Append printf-style text; once the buffer is full, len stays at cap and the caller sees it. */
static void append(char *buf, size_t cap, size_t *len, const char *fmt, ...)
{
    if (*len >= cap) {
        return;
    }
    va_list ap;
    va_start(ap, fmt);
    const int n = vsnprintf(buf + *len, cap - *len, fmt, ap);
    va_end(ap);
    *len = (n < 0 || (size_t)n >= cap - *len) ? cap : *len + (size_t)n;
}

/** @return length of the JSON in buf, or 0 if it did not fit */
static size_t encode_status(char *buf, size_t cap, const capture_status_t *s)
{
    size_t n = 0;
    append(buf, cap, &n,
           "{\"label\":\"%s\",\"auto\":%s,\"period\":%u,\"session\":%u,\"session_count\":%u,\"total\":%u,",
           s->label, s->auto_on ? "true" : "false", (unsigned)s->period_s, (unsigned)s->session_no,
           (unsigned)s->session_count, (unsigned)s->total_count);
    append(buf, cap, &n,
           "\"cam_ok\":%s,\"sd_ok\":%s,\"time_ok\":%s,\"locked\":%s,\"min_period\":%u,\"max_period\":%u,",
           s->cam_ok ? "true" : "false", s->sd_ok ? "true" : "false", s->time_valid ? "true" : "false",
           s->exposure_locked ? "true" : "false", (unsigned)DATASET_PERIOD_MIN_S, (unsigned)DATASET_PERIOD_MAX_S);
    append(buf, cap, &n, "\"sd_total_mb\":%u,\"sd_free_mb\":%u,\"lamps\":[",
           (unsigned)(s->sd_total_bytes >> 20), (unsigned)(s->sd_free_bytes >> 20));
    for (unsigned k = 0; k < APP_LAMP_COUNT; k++) {
        append(buf, cap, &n, "%s%u", k > 0U ? "," : "", (unsigned)s->lamp_pct[k]);
    }
    append(buf, cap, &n, "],\"last\":\"%s\",\"error\":\"%s\"}", s->last_file, s->error);
    return n >= cap ? 0U : n;
}

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
    return reply_json(req, status, (n > 0 && (size_t)n < sizeof(s_reply_buf)) ? s_reply_buf
                                                                               : "{\"ok\":false}");
}

/** Read the whole request body into s_body_buf (NUL-terminated). @return length, or -1 on error */
static int read_body(httpd_req_t *req)
{
    if (req->content_len == 0U || req->content_len > TUNE_HTTP_MAX_BODY) {
        return -1;
    }
    size_t got = 0;
    while (got < req->content_len) {
        const int r = httpd_req_recv(req, s_body_buf + got, req->content_len - got);
        if (r <= 0) {
            if (r == HTTPD_SOCK_ERR_TIMEOUT) {
                continue;
            }
            return -1;
        }
        got += (size_t)r;
    }
    s_body_buf[got] = '\0';
    return (int)got;
}

/** Parse the body as a JSON object. The caller frees it with cJSON_Delete(). */
static cJSON *read_json_body(httpd_req_t *req)
{
    const int len = read_body(req);
    if (len < 0) {
        return NULL;
    }
    cJSON *root = cJSON_ParseWithLength(s_body_buf, (size_t)len);
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
    if (!cJSON_IsNumber(item)) {
        return false;
    }
    const double v = item->valuedouble;
    if (v < 0.0 || v > (double)max || v != floor(v)) {
        return false;
    }
    *out = (uint32_t)v;
    return true;
}

/* ================================== Handlers ================================== */

static esp_err_t index_get(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    /* The page is embedded with a trailing NUL: do not send it */
    return httpd_resp_send(req, index_html_start, (ssize_t)(index_html_end - index_html_start - 1));
}

static esp_err_t status_get(httpd_req_t *req)
{
    capture_status_t st;
    capture_ctrl_get_status(&st);
    const size_t len = encode_status(s_status_buf, sizeof(s_status_buf), &st);
    if (len == 0U) {
        ESP_LOGE(TAG, "status JSON does not fit in %u bytes", (unsigned)STATUS_BUF_SIZE);
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "status too large");
    }
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, s_status_buf, (ssize_t)len);
}

/* frame2jpg_cb output sink: copy into the preview buffer. Returning less than len aborts the encoder. */
static size_t jpeg_sink_cb(void *arg, size_t index, const void *data, size_t len)
{
    (void)index;
    jpeg_sink_t *sink = (jpeg_sink_t *)arg;
    if (len == 0U) {
        return 0U; /* end-of-image marker */
    }
    if (sink->len + len > sink->cap) {
        return 0U; /* does not fit: encoder reports failure */
    }
    memcpy(sink->buf + sink->len, data, len);
    sink->len += len;
    return len;
}

static esp_err_t snapshot_get(httpd_req_t *req)
{
    camera_fb_t *fb = camera_cap_acquire(PREVIEW_LOCK_MS);
    if (fb == NULL) {
        return reply_error(req, "503 Service Unavailable", "camera busy");
    }
    camera_fb_t view;
    camera_cap_crop(fb, &view);
    const bool shrunk = gray_box_downscale(view.buf, APP_FRAME_WIDTH, APP_FRAME_HEIGHT, PREVIEW_SCALE,
                                           s_preview_gray, sizeof(s_preview_gray));
    camera_cap_release(fb); /* the camera is free again before the JPEG and WiFi work */
    if (!shrunk) {
        return reply_error(req, "500 Internal Server Error", "preview failed");
    }

    camera_fb_t small = {
        .buf = s_preview_gray,
        .len = sizeof(s_preview_gray),
        .width = APP_PREVIEW_WIDTH,
        .height = APP_PREVIEW_HEIGHT,
        .format = PIXFORMAT_GRAYSCALE,
    };
    s_preview.len = 0;
    const bool encoded = frame2jpg_cb(&small, APP_PREVIEW_JPEG_QUALITY, jpeg_sink_cb, &s_preview);
    if (!encoded || s_preview.len == 0U) {
        ESP_LOGW(TAG, "preview JPEG failed or larger than %u bytes", (unsigned)PREVIEW_JPEG_BYTES);
        return reply_error(req, "500 Internal Server Error", "preview failed");
    }
    httpd_resp_set_type(req, "image/jpeg");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, (const char *)s_preview.buf, (ssize_t)s_preview.len);
}

static esp_err_t capture_post(httpd_req_t *req)
{
    const esp_err_t err = capture_ctrl_capture_now();
    capture_status_t st;
    capture_ctrl_get_status(&st);
    if (err != ESP_OK) {
        return reply_error(req, "500 Internal Server Error",
                           st.error[0] != '\0' ? st.error : "Chụp ảnh lỗi");
    }
    const int n = snprintf(s_reply_buf, sizeof(s_reply_buf), "{\"ok\":true,\"file\":\"%s\"}", st.last_file);
    return reply_json(req, "200 OK", (n > 0 && (size_t)n < sizeof(s_reply_buf)) ? s_reply_buf : "{\"ok\":true}");
}

static esp_err_t auto_post(httpd_req_t *req)
{
    cJSON *root = read_json_body(req);
    if (root == NULL) {
        return reply_error(req, "400 Bad Request", "Yêu cầu không hợp lệ");
    }
    bool on = false;
    uint32_t period = 0;
    const bool has_on = json_get_bool(root, "on", &on);
    const bool has_period = json_get_uint(root, "period", DATASET_PERIOD_MAX_S, &period);
    cJSON_Delete(root);
    if (!has_on || (on && !has_period)) {
        return reply_error(req, "400 Bad Request", "Chu kỳ phải là số nguyên giây từ 1 đến 3600");
    }
    const esp_err_t err = capture_ctrl_set_auto(on, period);
    if (err == ESP_ERR_INVALID_ARG) {
        return reply_error(req, "400 Bad Request", "Chu kỳ phải là số nguyên giây từ 1 đến 3600");
    }
    if (err != ESP_OK) {
        capture_status_t st;
        capture_ctrl_get_status(&st);
        return reply_error(req, "409 Conflict", st.error[0] != '\0' ? st.error : "Không bật được");
    }
    return reply_json(req, "200 OK", "{\"ok\":true}");
}

static esp_err_t label_post(httpd_req_t *req)
{
    cJSON *root = read_json_body(req);
    if (root == NULL) {
        return reply_error(req, "400 Bad Request", "Yêu cầu không hợp lệ");
    }
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(root, "label");
    const esp_err_t err = cJSON_IsString(item) ? capture_ctrl_set_label(item->valuestring)
                                               : ESP_ERR_INVALID_ARG;
    cJSON_Delete(root);
    if (err != ESP_OK) {
        return reply_error(req, "400 Bad Request", "Nhãn phải là nguoi, xe hoặc nen");
    }
    return reply_json(req, "200 OK", "{\"ok\":true}");
}

static esp_err_t session_post(httpd_req_t *req)
{
    (void)read_body(req); /* the body is ignored; reading it keeps the connection clean */
    const esp_err_t err = capture_ctrl_new_session();
    if (err != ESP_OK) {
        capture_status_t st;
        capture_ctrl_get_status(&st);
        return reply_error(req, "500 Internal Server Error",
                           st.error[0] != '\0' ? st.error : "Không tạo được phiên mới");
    }
    return reply_json(req, "200 OK", "{\"ok\":true}");
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
    if (!valid || capture_ctrl_set_lamp(lamp, (uint8_t)pct) != ESP_OK) {
        return reply_error(req, "400 Bad Request", "Đèn từ 0 (tất cả) đến 4, độ sáng từ 0 đến 100");
    }
    return reply_json(req, "200 OK", "{\"ok\":true}");
}

static esp_err_t exposure_post(httpd_req_t *req)
{
    cJSON *root = read_json_body(req);
    if (root == NULL) {
        return reply_error(req, "400 Bad Request", "Yêu cầu không hợp lệ");
    }
    bool lock = false;
    const bool valid = json_get_bool(root, "lock", &lock);
    cJSON_Delete(root);
    if (!valid) {
        return reply_error(req, "400 Bad Request", "Thiếu trường lock");
    }
    if (capture_ctrl_set_exposure_lock(lock) != ESP_OK) {
        return reply_error(req, "500 Internal Server Error", "Không đổi được chế độ phơi sáng");
    }
    return reply_json(req, "200 OK", "{\"ok\":true}");
}

static esp_err_t time_post(httpd_req_t *req)
{
    if (read_body(req) < 0) {
        return reply_error(req, "400 Bad Request", "Yêu cầu không hợp lệ");
    }
    char *end = NULL;
    const long long epoch = strtoll(s_body_buf, &end, 10);
    if (end == s_body_buf || capture_ctrl_set_time(epoch) != ESP_OK) {
        return reply_error(req, "400 Bad Request", "Giờ không hợp lệ");
    }
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
        { .uri = "/snapshot.jpg", .method = HTTP_GET, .handler = snapshot_get },
        { .uri = "/api/status", .method = HTTP_GET, .handler = status_get },
        { .uri = "/api/capture", .method = HTTP_POST, .handler = capture_post },
        { .uri = "/api/auto", .method = HTTP_POST, .handler = auto_post },
        { .uri = "/api/label", .method = HTTP_POST, .handler = label_post },
        { .uri = "/api/session", .method = HTTP_POST, .handler = session_post },
        { .uri = "/api/lamp", .method = HTTP_POST, .handler = lamp_post },
        { .uri = "/api/exposure", .method = HTTP_POST, .handler = exposure_post },
        { .uri = "/api/time", .method = HTTP_POST, .handler = time_post },
    };
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
