/**
 * @file web_server.c
 * @brief HTTP server of tune-by-wifi. Commands reuse sl_codec_decode_cmd(), so the
 *        validation (clamping, lux gap, HH:MM) is the same as for the MQTT dashboard.
 *
 * All handlers run in the single httpd task, so the static buffers need no lock.
 */
#include "web_server.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_http_server.h"
#include "esp_log.h"
#include "node_ctrl.h"
#include "sl_mqtt_codec.h"
#include "tune_config.h"

#define TAG "web"

#define CMD_URI_PREFIX   "/api/cmd/"
#define STATUS_BUF_SIZE  1536U
#define HTTPD_STACK_SIZE 6144U

extern const char index_html_start[] asm("_binary_index_html_start");
extern const char index_html_end[] asm("_binary_index_html_end");

static const char *const MODE_TEXT[] = { "auto", "manual", "schedule" };

static char s_status_buf[STATUS_BUF_SIZE];
static char s_body_buf[TUNE_HTTP_MAX_BODY + 1U];

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

/* NaN / invalid values become JSON null */
static void append_num(char *buf, size_t cap, size_t *len, const char *key, bool valid, double v,
                       const char *fmt)
{
    if (valid && isfinite(v)) {
        append(buf, cap, len, "\"%s\":", key);
        append(buf, cap, len, fmt, v);
    } else {
        append(buf, cap, len, "\"%s\":null", key);
    }
}

/** @return length of the JSON in buf, or 0 if it did not fit */
static size_t encode_status(char *buf, size_t cap, const node_status_t *s)
{
    size_t n = 0;
    append(buf, cap, &n,
           "{\"night\":%s,\"force_night\":%s,\"mode\":\"%s\",\"manual_left\":%u,\"lux_ok\":%s,",
           s->night ? "true" : "false", s->force_night ? "true" : "false", MODE_TEXT[s->mode],
           (unsigned)s->manual_left_s, s->lux_ok ? "true" : "false");
    append_num(buf, cap, &n, "lux", s->lux_ok, (double)s->lux, "%.1f");
    append(buf, cap, &n, ",\"time_ok\":%s,\"minute\":%u,\"uptime\":%u,",
           s->time_valid ? "true" : "false", (unsigned)s->minute_of_day, (unsigned)s->uptime_s);
    append(buf, cap, &n,
           "\"config\":{\"lux_night\":%u,\"lux_day\":%u,\"fade_up_ms\":%u,\"fade_down_ms\":%u,"
           "\"night_start\":\"%s\",\"night_end\":\"%s\"},\"lamps\":[",
           (unsigned)s->config.lux_night, (unsigned)s->config.lux_day, (unsigned)s->config.fade_up_ms,
           (unsigned)s->config.fade_down_ms, s->config.night_start, s->config.night_end);
    for (unsigned k = 0; k < APP_LAMP_COUNT; k++) {
        append(buf, cap, &n, "%s{\"id\":%u,\"duty\":%u,\"ina_ok\":%s,", k ? "," : "", k + 1U,
               (unsigned)s->duty[k], s->ina_ok[k] ? "true" : "false");
        append_num(buf, cap, &n, "v", s->ina_ok[k], (double)s->v_volt[k], "%.3f");
        append(buf, cap, &n, ",");
        append_num(buf, cap, &n, "i", s->ina_ok[k], (double)s->i_ma[k], "%.2f");
        append(buf, cap, &n, ",");
        append_num(buf, cap, &n, "p", s->ina_ok[k], (double)s->p_mw[k], "%.1f");
        append(buf, cap, &n, ",\"e_today\":%.2f,\"e_total\":%.2f,", s->e_today_mwh[k], s->e_total_mwh[k]);
        append_num(buf, cap, &n, "saving", true, (double)s->saving_pct[k], "%.1f");
        append(buf, cap, &n, "}");
    }
    append(buf, cap, &n, "]}");
    return n >= cap ? 0U : n;
}

/* ================================ Handlers ================================ */

static esp_err_t index_get(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    /* The page is embedded with a trailing NUL: do not send it */
    return httpd_resp_send(req, index_html_start, (ssize_t)(index_html_end - index_html_start - 1));
}

static esp_err_t status_get(httpd_req_t *req)
{
    node_status_t st;
    node_ctrl_get_status(&st);
    const size_t len = encode_status(s_status_buf, sizeof(s_status_buf), &st);
    if (len == 0U) {
        ESP_LOGE(TAG, "status JSON does not fit in %u bytes", (unsigned)STATUS_BUF_SIZE);
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "status too large");
    }
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, s_status_buf, (ssize_t)len);
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

static esp_err_t reply_json(httpd_req_t *req, const char *status, const char *json)
{
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, json);
}

static esp_err_t cmd_post(httpd_req_t *req)
{
    if (strncmp(req->uri, CMD_URI_PREFIX, strlen(CMD_URI_PREFIX)) != 0) {
        return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "unknown command");
    }
    const char *name = req->uri + strlen(CMD_URI_PREFIX);
    const int len = read_body(req);
    if (len < 0) {
        return reply_json(req, "400 Bad Request", "{\"ok\":false,\"error\":\"bad body\"}");
    }
    sl_cmd_t cmd;
    if (!sl_codec_decode_cmd(name, strlen(name), s_body_buf, (size_t)len, &cmd)) {
        return reply_json(req, "400 Bad Request", "{\"ok\":false,\"error\":\"invalid command\"}");
    }
    const esp_err_t err = node_ctrl_apply_cmd(&cmd);
    if (err == ESP_ERR_INVALID_STATE) {
        return reply_json(req, "409 Conflict", "{\"ok\":false,\"error\":\"switch to manual mode first\"}");
    }
    if (err != ESP_OK) {
        return reply_json(req, "400 Bad Request", "{\"ok\":false,\"error\":\"rejected\"}");
    }
    return reply_json(req, "200 OK", "{\"ok\":true}");
}

static esp_err_t time_post(httpd_req_t *req)
{
    if (read_body(req) < 0) {
        return reply_json(req, "400 Bad Request", "{\"ok\":false,\"error\":\"bad body\"}");
    }
    char *end = NULL;
    const long long epoch = strtoll(s_body_buf, &end, 10);
    if (end == s_body_buf || node_ctrl_set_time(epoch) != ESP_OK) {
        return reply_json(req, "400 Bad Request", "{\"ok\":false,\"error\":\"bad time\"}");
    }
    return reply_json(req, "200 OK", "{\"ok\":true}");
}

/* ================================== Start ================================== */

esp_err_t web_server_start(void)
{
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.stack_size = HTTPD_STACK_SIZE;
    cfg.max_uri_handlers = 4;
    cfg.max_open_sockets = 4;
    cfg.lru_purge_enable = true; /* a phone that vanished must not block the next one */
    cfg.uri_match_fn = httpd_uri_match_wildcard;

    httpd_handle_t server = NULL;
    esp_err_t err = httpd_start(&server, &cfg);
    if (err != ESP_OK) {
        return err;
    }
    const httpd_uri_t routes[] = {
        { .uri = "/", .method = HTTP_GET, .handler = index_get },
        { .uri = "/api/status", .method = HTTP_GET, .handler = status_get },
        { .uri = CMD_URI_PREFIX "*", .method = HTTP_POST, .handler = cmd_post },
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
