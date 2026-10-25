/**
 * @file sl_mqtt_codec.c
 * @brief JSON encode/decode for the streetlight MQTT protocol (see sl_mqtt_codec.h).
 *
 * Encoders use snprintf into a caller buffer (no heap). Decoders use cJSON,
 * which allocates on the heap; this only runs when a dashboard command arrives
 * (rare, outside the control loop), never in an ISR.
 */
#include "sl_mqtt_codec.h"

#include <ctype.h>
#include <inttypes.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "cJSON.h"

const sl_config_t SL_CONFIG_DEFAULT = {
    .lux_night = 30,
    .lux_day = 100,
    .fade_up_ms = 200,
    .fade_down_ms = 3000,
    .night_start = "18:00",
    .night_end = "06:00",
};

static const char *const MODE_NAMES[] = {
    [SL_MODE_AUTO] = "auto",
    [SL_MODE_MANUAL] = "manual",
    [SL_MODE_SCHEDULE] = "schedule",
};

/* ======================= JSON writer (bounded, no heap) ======================= */

typedef struct {
    char *buf;
    size_t cap;
    size_t len;
    bool overflow;
} json_writer_t;

static void w_init(json_writer_t *w, char *buf, size_t cap)
{
    w->buf = buf;
    w->cap = cap;
    w->len = 0;
    w->overflow = (buf == NULL || cap == 0);
    if (!w->overflow) {
        buf[0] = '\0';
    }
}

static void w_fmt(json_writer_t *w, const char *fmt, ...)
{
    if (w->overflow) {
        return;
    }
    va_list ap;
    va_start(ap, fmt);
    const int n = vsnprintf(w->buf + w->len, w->cap - w->len, fmt, ap);
    va_end(ap);
    if (n < 0 || (size_t)n >= w->cap - w->len) {
        w->overflow = true;
        return;
    }
    w->len += (size_t)n;
}

/* Quoted JSON string with escaping of quotes, backslashes and control chars */
static void w_str(json_writer_t *w, const char *s)
{
    w_fmt(w, "\"");
    for (const unsigned char *p = (const unsigned char *)s; *p != '\0'; p++) {
        if (*p == '"' || *p == '\\') {
            w_fmt(w, "\\%c", *p);
        } else if (*p < 0x20U) {
            w_fmt(w, "\\u%04x", *p);
        } else {
            w_fmt(w, "%c", *p);
        }
    }
    w_fmt(w, "\"");
}

/* Optional string field: ,"key":"value" (skipped when value is NULL or empty) */
static void w_opt_str(json_writer_t *w, const char *key, const char *value)
{
    if (value != NULL && value[0] != '\0') {
        w_fmt(w, ",\"%s\":", key);
        w_str(w, value);
    }
}

static int w_finish(const json_writer_t *w)
{
    return w->overflow ? -1 : (int)w->len;
}

static const char *bool_str(bool v)
{
    return v ? "true" : "false";
}

/* ================================ Encoders ================================ */

int sl_codec_encode_state(char *buf, size_t cap, const sl_state_t *state)
{
    if (state == NULL || (unsigned)state->mode > SL_MODE_SCHEDULE) {
        return -1;
    }
    json_writer_t w;
    w_init(&w, buf, cap);
    w_fmt(&w, "{\"night\":%s,\"lux\":%" PRId32 ",\"mode\":\"%s\",\"force_night\":%s",
          bool_str(state->night), state->lux, MODE_NAMES[state->mode],
          bool_str(state->force_night));
    w_fmt(&w, ",\"duty\":[%u,%u,%u,%u]", (unsigned)state->duty[0], (unsigned)state->duty[1],
          (unsigned)state->duty[2], (unsigned)state->duty[3]);
    w_fmt(&w, ",\"persons\":%u,\"vehicles\":%u,\"zones\":[",
          (unsigned)state->persons, (unsigned)state->vehicles);
    bool first = true;
    for (unsigned z = 0; z < SL_ZONE_COUNT; z++) {
        if ((state->zone_mask & (1U << z)) != 0U) {
            w_fmt(&w, first ? "%u" : ",%u", z + 1U);
            first = false;
        }
    }
    w_fmt(&w, "],\"fps\":%.1f", (double)state->fps);
    w_opt_str(&w, "sensor_error", state->sensor_error);
    w_fmt(&w, "}");
    return w_finish(&w);
}

static void encode_lamp(json_writer_t *w, unsigned id, const sl_lamp_energy_t *l)
{
    w_fmt(w, "{\"id\":%u,\"duty\":%u,\"ina_ok\":%s", id, (unsigned)l->duty, bool_str(l->ina_ok));
    if (l->ina_ok) {
        w_fmt(w, ",\"v\":%.3f,\"i\":%.2f,\"p\":%.1f",
              (double)l->v_volt, (double)l->i_ma, (double)l->p_mw);
    } else {
        w_fmt(w, ",\"v\":null,\"i\":null");
    }
    w_fmt(w, ",\"e_today\":%" PRIu32 ",\"e_total\":%" PRIu32, l->e_today_mwh, l->e_total_mwh);
    if (!isnan(l->saving_pct)) {
        w_fmt(w, ",\"saving\":%.1f", (double)l->saving_pct);
    }
    if (l->fault >= 0) {
        w_fmt(w, ",\"fault\":%d", (int)l->fault);
    }
    w_fmt(w, "}");
}

int sl_codec_encode_energy(char *buf, size_t cap, const sl_lamp_energy_t lamps[SL_LAMP_COUNT])
{
    if (lamps == NULL) {
        return -1;
    }
    json_writer_t w;
    w_init(&w, buf, cap);
    w_fmt(&w, "{\"lamps\":[");
    for (unsigned k = 0; k < SL_LAMP_COUNT; k++) {
        if (k > 0U) {
            w_fmt(&w, ",");
        }
        encode_lamp(&w, k + 1U, &lamps[k]);
    }
    w_fmt(&w, "]}");
    return w_finish(&w);
}

int sl_codec_encode_history(char *buf, size_t cap, int64_t t_sec,
                            const uint32_t mwh[SL_LAMP_COUNT][SL_HISTORY_HOURS])
{
    if (mwh == NULL) {
        return -1;
    }
    json_writer_t w;
    w_init(&w, buf, cap);
    w_fmt(&w, "{\"t\":%lld,\"lamps\":[", (long long)t_sec);
    for (unsigned k = 0; k < SL_LAMP_COUNT; k++) {
        w_fmt(&w, k > 0U ? ",[" : "[");
        for (unsigned h = 0; h < SL_HISTORY_HOURS; h++) {
            w_fmt(&w, h > 0U ? ",%" PRIu32 : "%" PRIu32, mwh[k][h]);
        }
        w_fmt(&w, "]");
    }
    w_fmt(&w, "]}");
    return w_finish(&w);
}

int sl_codec_encode_config(char *buf, size_t cap, const sl_config_t *cfg)
{
    if (cfg == NULL) {
        return -1;
    }
    json_writer_t w;
    w_init(&w, buf, cap);
    w_fmt(&w, "{\"lux_night\":%u,\"lux_day\":%u,\"fade_up_ms\":%u,\"fade_down_ms\":%u",
          (unsigned)cfg->lux_night, (unsigned)cfg->lux_day,
          (unsigned)cfg->fade_up_ms, (unsigned)cfg->fade_down_ms);
    w_fmt(&w, ",\"night_start\":");
    w_str(&w, cfg->night_start);
    w_fmt(&w, ",\"night_end\":");
    w_str(&w, cfg->night_end);
    w_fmt(&w, "}");
    return w_finish(&w);
}

int sl_codec_encode_alert(char *buf, size_t cap, const sl_alert_t *alert)
{
    if (alert == NULL || alert->type == NULL) {
        return -1;
    }
    json_writer_t w;
    w_init(&w, buf, cap);
    w_fmt(&w, "{\"id\":%" PRIu32 ",\"type\":", alert->id);
    w_str(&w, alert->type);
    if (alert->zone != 0U) {
        w_fmt(&w, ",\"zone\":%u", (unsigned)alert->zone);
    }
    w_opt_str(&w, "class", alert->cls);
    if (alert->since != 0) {
        w_fmt(&w, ",\"since\":%lld", (long long)alert->since);
    }
    w_opt_str(&w, "status", alert->status);
    w_opt_str(&w, "msg", alert->msg);
    w_fmt(&w, "}");
    return w_finish(&w);
}

/* ================================ Decoders ================================ */

static bool name_is(const char *name, size_t len, const char *literal)
{
    return strlen(literal) == len && memcmp(name, literal, len) == 0;
}

/* Compare a plain-text payload, ignoring surrounding spaces and quotes */
static bool text_is(const char *data, size_t len, const char *literal)
{
    while (len > 0U && isspace((unsigned char)data[0])) {
        data++;
        len--;
    }
    while (len > 0U && isspace((unsigned char)data[len - 1U])) {
        len--;
    }
    if (len >= 2U && data[0] == '"' && data[len - 1U] == '"') {
        data++;
        len -= 2U;
    }
    return name_is(data, len, literal);
}

static bool is_hhmm(const char *s)
{
    return strlen(s) == 5U
        && s[0] >= '0' && s[0] <= '2' && isdigit((unsigned char)s[1])
        && (s[0] < '2' || s[1] <= '3')
        && s[2] == ':'
        && s[3] >= '0' && s[3] <= '5' && isdigit((unsigned char)s[4]);
}

static double clampd(double x, double lo, double hi)
{
    return x < lo ? lo : (x > hi ? hi : x);
}

/* Read an optional numeric field, clamp it and flag it in the patch */
static void read_num(const cJSON *obj, const char *key, double lo, double hi,
                     uint16_t *dst, uint8_t flag, uint8_t *fields)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (cJSON_IsNumber(item) && isfinite(item->valuedouble)) {
        *dst = (uint16_t)lround(clampd(item->valuedouble, lo, hi));
        *fields |= flag;
    }
}

static void read_hhmm(const cJSON *obj, const char *key, char dst[SL_HHMM_LEN],
                      uint8_t flag, uint8_t *fields)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (cJSON_IsString(item) && is_hhmm(item->valuestring)) {
        memcpy(dst, item->valuestring, SL_HHMM_LEN);
        *fields |= flag;
    }
}

static bool decode_lamp(const cJSON *root, sl_cmd_t *out)
{
    const cJSON *lamp = cJSON_GetObjectItemCaseSensitive(root, "lamp");
    const cJSON *duty = cJSON_GetObjectItemCaseSensitive(root, "duty");
    if (!cJSON_IsNumber(lamp) || !cJSON_IsNumber(duty) || !isfinite(duty->valuedouble)) {
        return false;
    }
    const double id = lamp->valuedouble;
    if (id != floor(id) || id < 1.0 || id > (double)SL_LAMP_COUNT
        || duty->valuedouble < 0.0 || duty->valuedouble > 100.0) {
        return false;
    }
    out->u.lamp.lamp = (uint8_t)id;
    out->u.lamp.duty = (uint8_t)lround(duty->valuedouble);
    return true;
}

static bool decode_ack(const cJSON *root, sl_cmd_t *out)
{
    const cJSON *id = cJSON_GetObjectItemCaseSensitive(root, "id");
    int n = -1;
    if (cJSON_IsString(id)) {
        n = snprintf(out->u.alert_id, SL_ALERT_ID_LEN, "%s", id->valuestring);
    } else if (cJSON_IsNumber(id) && id->valuedouble == floor(id->valuedouble)
               && fabs(id->valuedouble) < 1e15) {
        n = snprintf(out->u.alert_id, SL_ALERT_ID_LEN, "%lld", (long long)id->valuedouble);
    }
    return n > 0 && n < SL_ALERT_ID_LEN;
}

static bool decode_config(const cJSON *root, sl_cmd_t *out)
{
    sl_config_patch_t *p = &out->u.config;
    if (cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(root, "reset"))) {
        p->fields = SL_CFG_RESET;
        return true;
    }
    read_num(root, "lux_night", SL_LUX_MIN, SL_LUX_MAX, &p->value.lux_night, SL_CFG_LUX_NIGHT, &p->fields);
    read_num(root, "lux_day", SL_LUX_MIN, SL_LUX_MAX, &p->value.lux_day, SL_CFG_LUX_DAY, &p->fields);
    read_num(root, "fade_up_ms", 0, SL_FADE_UP_MAX_MS, &p->value.fade_up_ms, SL_CFG_FADE_UP, &p->fields);
    read_num(root, "fade_down_ms", 0, SL_FADE_DOWN_MAX_MS, &p->value.fade_down_ms, SL_CFG_FADE_DOWN, &p->fields);
    read_hhmm(root, "night_start", p->value.night_start, SL_CFG_NIGHT_START, &p->fields);
    read_hhmm(root, "night_end", p->value.night_end, SL_CFG_NIGHT_END, &p->fields);
    return p->fields != 0U;
}

/* Commands whose payload is a JSON object */
static bool decode_json_cmd(sl_cmd_type_t type, const char *data, size_t len, sl_cmd_t *out)
{
    cJSON *root = cJSON_ParseWithLength(data, len);
    if (root == NULL) {
        return false;
    }
    bool ok = false;
    if (cJSON_IsObject(root)) {
        out->type = type;
        switch (type) {
        case SL_CMD_LAMP:      ok = decode_lamp(root, out); break;
        case SL_CMD_ALERT_ACK: ok = decode_ack(root, out); break;
        case SL_CMD_CONFIG:    ok = decode_config(root, out); break;
        default:               ok = false; break;
        }
    }
    cJSON_Delete(root);
    return ok;
}

bool sl_codec_decode_cmd(const char *name, size_t name_len,
                         const char *data, size_t data_len, sl_cmd_t *out)
{
    if (name == NULL || out == NULL || data == NULL || data_len == 0U) {
        return false;
    }
    memset(out, 0, sizeof(*out));

    if (name_is(name, name_len, "mode")) {
        out->type = SL_CMD_MODE;
        if (text_is(data, data_len, "auto")) {
            out->u.mode = SL_MODE_AUTO;
            return true;
        }
        if (text_is(data, data_len, "manual")) {
            out->u.mode = SL_MODE_MANUAL;
            return true;
        }
        return false;
    }
    if (name_is(name, name_len, "force_night")) {
        out->type = SL_CMD_FORCE_NIGHT;
        out->u.force_night = text_is(data, data_len, "true");
        return out->u.force_night || text_is(data, data_len, "false");
    }
    if (name_is(name, name_len, "lamp")) {
        return decode_json_cmd(SL_CMD_LAMP, data, data_len, out);
    }
    if (name_is(name, name_len, "alert_ack")) {
        return decode_json_cmd(SL_CMD_ALERT_ACK, data, data_len, out);
    }
    if (name_is(name, name_len, "config")) {
        return decode_json_cmd(SL_CMD_CONFIG, data, data_len, out);
    }
    return false;
}

/* ============================ Config validation ============================ */

void sl_codec_apply_config(const sl_config_t *cur, const sl_config_patch_t *patch,
                           sl_config_t *out)
{
    if (cur == NULL || patch == NULL || out == NULL) {
        return;
    }
    if ((patch->fields & SL_CFG_RESET) != 0U) {
        *out = SL_CONFIG_DEFAULT;
        return;
    }
    const uint8_t f = patch->fields;
    const sl_config_t *v = &patch->value;
    sl_config_t c = *cur;

    if ((f & SL_CFG_LUX_NIGHT) != 0U)   { c.lux_night = v->lux_night; }
    if ((f & SL_CFG_LUX_DAY) != 0U)     { c.lux_day = v->lux_day; }
    if ((f & SL_CFG_FADE_UP) != 0U)     { c.fade_up_ms = v->fade_up_ms; }
    if ((f & SL_CFG_FADE_DOWN) != 0U)   { c.fade_down_ms = v->fade_down_ms; }
    if ((f & SL_CFG_NIGHT_START) != 0U) { memcpy(c.night_start, v->night_start, SL_HHMM_LEN); }
    if ((f & SL_CFG_NIGHT_END) != 0U)   { memcpy(c.night_end, v->night_end, SL_HHMM_LEN); }

    /* Night start and end must differ, otherwise the night window is empty:
     * reject the time change and keep the current (valid) pair */
    if (strcmp(c.night_start, c.night_end) == 0) {
        memcpy(c.night_start, cur->night_start, SL_HHMM_LEN);
        memcpy(c.night_end, cur->night_end, SL_HHMM_LEN);
    }

    /* Hysteresis: lux_day must stay at least SL_LUX_MIN_GAP above lux_night */
    if ((int32_t)c.lux_day - (int32_t)c.lux_night < SL_LUX_MIN_GAP) {
        const bool only_night_changed = (f & SL_CFG_LUX_NIGHT) != 0U && (f & SL_CFG_LUX_DAY) == 0U;
        if (only_night_changed) {
            const int32_t day = (int32_t)c.lux_night + SL_LUX_MIN_GAP;
            c.lux_day = (uint16_t)(day > SL_LUX_MAX ? SL_LUX_MAX : day);
        } else {
            const int32_t night = (int32_t)c.lux_day - SL_LUX_MIN_GAP;
            c.lux_night = (uint16_t)(night < SL_LUX_MIN ? SL_LUX_MIN : night);
        }
        if ((int32_t)c.lux_day - (int32_t)c.lux_night < SL_LUX_MIN_GAP) {
            c.lux_day = (uint16_t)(c.lux_night + SL_LUX_MIN_GAP);
        }
    }
    *out = c;
}
