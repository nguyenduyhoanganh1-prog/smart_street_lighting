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
    const char *sensor_error;    /* "bh1750" / "camera", NULL when none */
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
    char night_start[SL_HHMM_LEN];
    char night_end[SL_HHMM_LEN];
} sl_config_t;

/** Payload of streetlight/alert */
typedef struct {
    uint32_t id;
    const char *type;      /* "vat_dung" | "camera" | "bh1750" | "mat_gio" | "ina219" */
    uint8_t zone;          /* 1..4, 0 = omit */
    const char *cls;       /* "nguoi" / "xe", NULL = omit */
    int64_t since;         /* Unix seconds, 0 = omit */
    const char *status;    /* NULL = omit */
    const char *msg;       /* NULL = omit */
} sl_alert_t;

/* ---------------- Commands received from the dashboard ---------------- */

typedef enum {
    SL_CMD_MODE = 0,       /* streetlight/cmd/mode */
    SL_CMD_LAMP,           /* streetlight/cmd/lamp */
    SL_CMD_FORCE_NIGHT,    /* streetlight/cmd/force_night */
    SL_CMD_ALERT_ACK,      /* streetlight/cmd/alert_ack */
    SL_CMD_CONFIG,         /* streetlight/cmd/config */
} sl_cmd_type_t;

/* Bits of sl_config_patch_t.fields: which values the dashboard wants to change */
#define SL_CFG_LUX_NIGHT   (1U << 0)
#define SL_CFG_LUX_DAY     (1U << 1)
#define SL_CFG_FADE_UP     (1U << 2)
#define SL_CFG_FADE_DOWN   (1U << 3)
#define SL_CFG_NIGHT_START (1U << 4)
#define SL_CFG_NIGHT_END   (1U << 5)
#define SL_CFG_RESET       (1U << 7)

typedef struct {
    uint8_t fields;        /* SL_CFG_* bits */
    sl_config_t value;     /* only fields flagged above are valid (already clamped) */
} sl_config_patch_t;

typedef struct {
    sl_cmd_type_t type;
    union {
        sl_mode_t mode;                            /* SL_CMD_MODE: auto/manual only */
        struct { uint8_t lamp; uint8_t duty; } lamp; /* SL_CMD_LAMP: lamp 1..4, duty 0..100 */
        bool force_night;                          /* SL_CMD_FORCE_NIGHT */
        char alert_id[SL_ALERT_ID_LEN];            /* SL_CMD_ALERT_ACK */
        sl_config_patch_t config;                  /* SL_CMD_CONFIG */
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
