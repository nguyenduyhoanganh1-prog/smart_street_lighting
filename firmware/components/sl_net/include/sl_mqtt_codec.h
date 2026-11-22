/**
 * @file sl_mqtt_codec.h
 * @brief JSON encode/decode for the streetlight MQTT protocol.
 *
 * Mirrors dashboard/src/lib/protocol.ts. Pure C + cJSON, no ESP-IDF calls,
 * so every function here can be unit-tested on a PC.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SL_LAMP_COUNT    4
#define SL_ZONE_COUNT    4
#define SL_HISTORY_HOURS 24
#define SL_ALERT_ID_LEN  24
#define SL_HHMM_LEN      6 /* "HH:MM" + NUL */

/* Limits shared with the dashboard (protocol.ts LIMITS) */
#define SL_LUX_MIN          0
#define SL_LUX_MAX          1000
#define SL_LUX_MIN_GAP      20
#define SL_FADE_UP_MAX_MS   2000
#define SL_FADE_DOWN_MAX_MS 10000
#define SL_BASE_MIN_PCT     10  /* night base level of a lamp with nobody near it */
#define SL_BASE_MAX_PCT     100
#define SL_STOP_TIME_MIN_S  10    /* stopped-object alert: still this long before the alert */
#define SL_STOP_TIME_MAX_S  1800

typedef enum {
    SL_MODE_AUTO = 0,
    SL_MODE_MANUAL,
    SL_MODE_SCHEDULE,
} sl_mode_t;

/** Payload of streetlight/state */
typedef struct {
    bool night;
    int32_t lux;
    sl_mode_t mode;
    bool force_night;
    uint8_t duty[SL_LAMP_COUNT]; /* brightness per lamp, 0..100 % */
    uint16_t persons;
    uint16_t vehicles;
    uint8_t zone_mask;           /* bit0 = zone 1 ... bit3 = zone 4 */
    float fps;
    const char *sensor_error;    /* "bh1750" (offline) / "bh1750_dark" (dark by day) / "camera", NULL when none */
    int64_t t_unix_ms;           /* send time, Unix ms (latency test, plans/eval.md 6.4); 0 = omit */
} sl_state_t;

/** One lamp inside streetlight/energy */
typedef struct {
    uint8_t duty;          /* 0..100 % */
    bool ina_ok;           /* false -> v/i sent as null */
    float v_volt;          /* bus voltage, V */
    float i_ma;            /* current, mA */
    float p_mw;            /* power, mW */
    uint32_t e_today_mwh;
    uint32_t e_total_mwh;
    float saving_pct;      /* NAN when unknown -> field omitted */
    int8_t fault;          /* 0 = ok, 1..5 see protocol.ts; -1 = let the dashboard detect */
} sl_lamp_energy_t;

/** Payload of streetlight/config */
typedef struct {
    uint16_t lux_night;
    uint16_t lux_day;
    uint16_t fade_up_ms;
    uint16_t fade_down_ms;
    uint16_t base_pct;     /* night base level, SL_BASE_MIN_PCT..SL_BASE_MAX_PCT % */
    uint16_t stop_alert;   /* stopped-object alert on (1) / off (0) */
    uint16_t stop_time_s;  /* still this long -> alert, SL_STOP_TIME_MIN_S..SL_STOP_TIME_MAX_S */
    char night_start[SL_HHMM_LEN];
    char night_end[SL_HHMM_LEN];
} sl_config_t;

#define SL_ALERT_MAX_OBJS 10 /* = DETECTOR_MAX_OBJECTS */

/** One detection in the alert picture (vat_dung): all of them get a dot on the dashboard */
typedef struct {
    float x;               /* 0..1 of the image width */
    float y;               /* 0..1 of the image height */
    bool vehicle;          /* false = person */
    uint8_t score;         /* % */
    uint8_t zone;          /* 1..4, 0 = omit (off the road) */
    int64_t since;         /* Unix seconds it has been still since, 0 = omit (no SNTP time) */
    bool has_still;        /* still_s valid: the tracker follows this vehicle */
    uint32_t still_s;      /* still for this long when sent; needs no clock, so it works without SNTP */
} sl_alert_obj_t;

/** Payload of streetlight/alert */
typedef struct {
    uint32_t id;
    const char *type;      /* "vat_dung" | "camera" | "bh1750" | "mat_gio" | "ina219" */
    uint8_t zone;          /* 1..4, 0 = omit */
    const char *cls;       /* "nguoi" / "xe", NULL = omit */
    int64_t since;         /* Unix seconds, 0 = omit */
    const char *status;    /* NULL = omit */
    const char *msg;       /* NULL = omit */
    bool has_pos;          /* x, y, score below are valid (vat_dung: where the object is in alert/image) */
    float x;               /* object centre, 0..1 of the image width */
    float y;               /* 0..1 of the image height */
    uint8_t score;         /* detection confidence, % */
    uint32_t still_s;      /* has_pos: still for this long when sent (works without SNTP) */
    uint8_t obj_count;     /* objs[] below: every detection of the picture's frame, 0 = omit */
    sl_alert_obj_t objs[SL_ALERT_MAX_OBJS];
} sl_alert_t;

/**
 * Payload of streetlight/calib: result of one road calibration, in pixels of the calibration frame
 * (the JPEG sent to streetlight/calib/image). Same fields as GET /api/road of the ai-stream page.
 * Lines are y = a * x + b, except the left end x = a * y + b.
 */
typedef struct {
    bool found;                       /* false: only state, w, h and t are sent */
    uint16_t width, height;
    float top_a, top_b;               /* road edges */
    float bottom_a, bottom_b;
    bool has_left;
    float left_a, left_b;
    bool has_kerb_top;                /* outer kerb edges; a missing side is sent as null */
    float kerb_top_a, kerb_top_b;
    bool has_kerb_bottom;
    float kerb_bottom_a, kerb_bottom_b;
    int16_t x_min, x_max;
    float bounds[SL_ZONE_COUNT - 1];  /* x of the zone cuts */
    int64_t t_sec;                    /* Unix seconds of the calibration, 0 = clock unknown (omitted) */
} sl_calib_t;

/* ---------------- Commands received from the dashboard ---------------- */

typedef enum {
    SL_CMD_MODE = 0,       /* streetlight/cmd/mode */
    SL_CMD_LAMP,           /* streetlight/cmd/lamp */
    SL_CMD_FORCE_NIGHT,    /* streetlight/cmd/force_night */
    SL_CMD_ALERT_ACK,      /* streetlight/cmd/alert_ack */
    SL_CMD_CONFIG,         /* streetlight/cmd/config */
    SL_CMD_RECALIBRATE,    /* streetlight/cmd/recalibrate: find the road again (any non-empty payload) */
    SL_CMD_LATENCY,        /* streetlight/cmd/latency: {"t":<state t>,"rx":<dashboard got it>} (eval only) */
} sl_cmd_type_t;

/* Bits of sl_config_patch_t.fields: which values the dashboard wants to change */
#define SL_CFG_LUX_NIGHT   (1U << 0)
#define SL_CFG_LUX_DAY     (1U << 1)
#define SL_CFG_FADE_UP     (1U << 2)
#define SL_CFG_FADE_DOWN   (1U << 3)
#define SL_CFG_NIGHT_START (1U << 4)
#define SL_CFG_NIGHT_END   (1U << 5)
#define SL_CFG_BASE        (1U << 6)
#define SL_CFG_STOP_ALERT  (1U << 8)
#define SL_CFG_STOP_TIME   (1U << 9)
#define SL_CFG_RESET       (1U << 7)

typedef struct {
    uint16_t fields;       /* SL_CFG_* bits */
    sl_config_t value;     /* only fields flagged above are valid (already clamped) */
} sl_config_patch_t;

typedef struct {
    sl_cmd_type_t type;
    /* Latency log (plans/eval.md 6.5). sent_unix_ms: optional "t" of a JSON command, Unix ms when the
     * dashboard sent it (0 = not given). rx_unix_ms: set by sl_mqtt.c the moment the message arrived
     * (the decoder leaves it 0). */
    int64_t sent_unix_ms;
    int64_t rx_unix_ms;
    union {
        sl_mode_t mode;                            /* SL_CMD_MODE: auto/manual only */
        struct { uint8_t lamp; uint8_t duty; } lamp; /* SL_CMD_LAMP: lamp 1..4, duty 0..100 */
        bool force_night;                          /* SL_CMD_FORCE_NIGHT */
        char alert_id[SL_ALERT_ID_LEN];            /* SL_CMD_ALERT_ACK */
        sl_config_patch_t config;                  /* SL_CMD_CONFIG */
        struct { int64_t board_ms; int64_t web_ms; } latency; /* SL_CMD_LATENCY: Unix ms */
    } u;
} sl_cmd_t;

/** Factory defaults, same as DEFAULT_CONFIG in protocol.ts */
extern const sl_config_t SL_CONFIG_DEFAULT;

/*
 * Encoders: write JSON into buf (NUL-terminated).
 * Return the payload length, or -1 if buf is too small or an argument is NULL.
 */
int sl_codec_encode_state(char *buf, size_t cap, const sl_state_t *state);
int sl_codec_encode_energy(char *buf, size_t cap, const sl_lamp_energy_t lamps[SL_LAMP_COUNT]);
int sl_codec_encode_history(char *buf, size_t cap, int64_t t_sec,
                            const uint32_t mwh[SL_LAMP_COUNT][SL_HISTORY_HOURS]);
int sl_codec_encode_config(char *buf, size_t cap, const sl_config_t *cfg);
int sl_codec_encode_alert(char *buf, size_t cap, const sl_alert_t *alert);
int sl_codec_encode_calib(char *buf, size_t cap, const sl_calib_t *calib);

/**
 * Decode one command.
 * @param name      topic suffix after "streetlight/cmd/" (not NUL-terminated)
 * @param data      raw MQTT payload (not NUL-terminated)
 * @return true and fills *out when the command is valid, false otherwise
 */
bool sl_codec_decode_cmd(const char *name, size_t name_len,
                         const char *data, size_t data_len, sl_cmd_t *out);

/**
 * Apply a config patch exactly like applyConfig() in protocol.ts:
 * reset, keep night_start != night_end, keep lux_day - lux_night >= SL_LUX_MIN_GAP.
 * cur and out may point to the same struct.
 */
void sl_codec_apply_config(const sl_config_t *cur, const sl_config_patch_t *patch,
                           sl_config_t *out);

#ifdef __cplusplus
}
#endif
