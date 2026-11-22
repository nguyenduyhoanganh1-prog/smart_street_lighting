/**
 * @file main.c
 * @brief streetlight-node: BH1750 + 4x INA219 + 4 PWM lamps, camera + FOMO zone lighting,
 *        energy bookkeeping (NVS), SNTP time, MQTT telemetry and commands.
 *
 * Three tasks (plans/04 diagram 6), so that no slow job can stretch the control cycle:
 *   control  priority 6  exactly every APP_SAMPLE_PERIOD_MS: sensors -> clock -> vision mailbox ->
 *                        lighting -> PWM -> energy -> P100 -> NVS. Never waits for the network or the camera.
 *   vision   priority 4  (vision.c) camera + FOMO + road calibration, result into a mailbox.
 *   network  priority 3  WiFi/SNTP/MQTT: publishes what the control task prepared and forwards the
 *                        dashboard commands through a queue. TLS publishes may block for a second.
 *
 * Faults are handled separately: BH1750 fault -> day/night from the clock, the camera zones keep working;
 * camera or model fault -> no zones, night lamps full (state.sensor_error "camera"); both -> clock + full.
 * Stopped object on the road for config.stop_time_s (stop_detect.h) -> vat_dung alert + picture (switchable).
 * Not implemented yet: vehicle direction tracking.
 *
 * Two modes, chosen at start (boot_mode.h): normal (all of the above) or ai-stream (stream_mode.h: camera
 * web page on its own WiFi AP). A double click on IO0 (mode_button.h) restarts into the other one.
 */
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#include "app_config.h"
#include "boot_mode.h"
#include "detector.h"
#include "energy_acc.h"
#include "energy_stats.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "ina_zero.h"
#include "lamp_fault.h"
#include "lamp_pwm.h"
#include "lighting.h"
#include "log_view.h"
#include "lux_check.h"
#include "mode_button.h"
#include "node_store.h"
#include "nvs_flash.h"
#include "sensor_hub.h"
#include "sl_mqtt.h"
#include "sl_secrets.h"
#include "sl_wifi.h"
#include "stop_detect.h"
#include "stream_mode.h"
#include "time_sync.h"
#include "vision.h"
#include "zone_rule.h"

#define TAG "node"

/* Print a line only in the log view of its plans/eval.md section (log_view.h, typed in the monitor) */
#define LOGI_IN(sec, ...) do { if (log_view_show(sec)) { ESP_LOGI(TAG, __VA_ARGS__); } } while (0)
#define LOGW_IN(sec, ...) do { if (log_view_show(sec)) { ESP_LOGW(TAG, __VA_ARGS__); } } while (0)

#define CONTROL_TASK_STACK  8192U
#define CONTROL_TASK_PRIO   6U
#define NETWORK_TASK_STACK  8192U
#define NETWORK_TASK_PRIO   3U
#define NETWORK_PERIOD_MS   100U
#define COMMAND_QUEUE_LEN   8U
#define TX_LOCK_MS          50U

_Static_assert(APP_LAMP_COUNT == SL_LAMP_COUNT, "lamp count differs from the MQTT protocol");
_Static_assert(APP_LAMP_COUNT == ENERGY_ACC_CHANNELS, "lamp count differs from energy_acc");
_Static_assert(APP_LAMP_COUNT == ENERGY_LAMPS, "lamp count differs from energy_stats");
_Static_assert(APP_LAMP_COUNT == ZONE_RULE_LAMPS, "lamp count differs from the zone rule");
_Static_assert(ENERGY_HOURS == SL_HISTORY_HOURS, "history length differs from the MQTT protocol");

typedef struct {
    bool valid;              /* SNTP synced */
    int64_t epoch_s;
    uint16_t minute_of_day;  /* local */
    int32_t day_key;         /* local yyyymmdd */
} clock_info_t;

typedef struct {
    sl_config_t config;
    bool manual;                          /* user selected manual mode */
    int64_t manual_until_ms;
    uint8_t manual_duty[APP_LAMP_COUNT];
    bool force_night;
    bool night;                           /* effective night (sensor, clock or forced) */
    sl_mode_t mode;                       /* reported: auto / manual / schedule */
} node_state_t;

typedef struct {
    int64_t since_ms;                     /* -1 = lamp not at steady 100 % */
    double sum_mw;
    uint32_t samples;
} p100_cal_t;

/* What the control task hands to the network task. A ready flag stays set until it is published. */
typedef struct {
    bool telemetry_ready;
    sl_state_t state;
    sl_lamp_energy_t lamps[SL_LAMP_COUNT];
    bool config_ready;
    sl_config_t config;
    bool history_ready;
    int64_t history_t_sec;
    uint32_t history[SL_LAMP_COUNT][SL_HISTORY_HOURS];
    bool alert_ready;               /* alert_ready and alert_clear are never set together */
    sl_alert_t alert;               /* .msg points into alert_msg (fixed up by the network task) */
    uint32_t alert_image_id;        /* picture of this alert goes to alert/image (vision), 0 = none */
    char alert_msg[192];
    bool alert_clear;
} tx_box_t;

static const char *const MODE_TEXT[] = {
    [SL_MODE_AUTO] = "auto",
    [SL_MODE_MANUAL] = "manual",
    [SL_MODE_SCHEDULE] = "schedule",
};

static const char *const ROAD_TEXT[] = {
    [VISION_ROAD_PENDING] = "pending",
    [VISION_ROAD_FOUND] = "found",
    [VISION_ROAD_NOT_FOUND] = "not found (quarters)",
};

static const int LAMP_GPIOS[APP_LAMP_COUNT] = {
    APP_LAMP1_GPIO, APP_LAMP2_GPIO, APP_LAMP3_GPIO, APP_LAMP4_GPIO,
};

/* Large or long-lived state is static (no heap, small task stacks).
 * Written only by the control task: s_snap, s_energy_acc, s_stats, s_daynight, s_node, s_p100, s_fault, s_rule,
 * s_vis_*.
 * Written only by the network task (after init): s_secrets, s_mqtt_started. */
static sl_secrets_t s_secrets;
static bool s_secrets_ok;
static bool s_nvs_ok;
static bool s_mqtt_started;
static bool s_time_was_valid;
static sensor_snapshot_t s_snap;
static energy_acc_t s_energy_acc;
static energy_stats_t s_stats;
static bool s_stats_dirty;
static daynight_t s_daynight;
static node_state_t s_node;
static p100_cal_t s_p100[APP_LAMP_COUNT];
static lamp_fault_state_t s_fault[APP_LAMP_COUNT];
static lux_check_t s_lux_check;
static ina_zero_t s_ina_zero[APP_LAMP_COUNT];

static stop_detect_t s_stop;
static uint32_t s_stop_alert_seq;
static sl_alert_t s_open_alert;       /* last vat_dung alert raised (control task) */
static uint32_t s_objs_pending_id;    /* its vehicle list still waits for the picture's frame, 0 = none */

static void queue_alert(const sl_alert_t *alert);
static void queue_alert_clear(void);
static void on_lux_suspect_changed(bool suspect, const clock_info_t *clk);
static int64_t s_last_telemetry_ms;
static bool s_history_due;

static zone_rule_t s_rule;
static zone_rule_stats_t s_vis_stats;

/* Latency log (plans/eval.md 3.1): the frame that switched lamps from base to full, followed until the
 * PWM command. Control task only. */
typedef struct {
    bool pending;
    uint8_t lamps;      /* bit k = lamp k+1 newly lit */
    uint8_t objects;    /* detections in that frame */
    int64_t frame_ms;   /* frame taken from the camera */
    int64_t result_ms;  /* FOMO result ready */
    int64_t seen_ms;    /* control cycle that fed it to the zone rule */
} latency_probe_t;
static latency_probe_t s_latency;

/* Web -> lamp latency (plans/eval.md 6.5): the last lamp command, followed until its PWM is set.
 * Control task only. */
typedef struct {
    bool pending;
    uint8_t lamp;          /* 0..3 */
    uint8_t duty;
    int64_t sent_unix_ms;  /* dashboard send time, 0 = not given */
    int64_t rx_unix_ms;    /* board got the MQTT message, 0 = no SNTP time */
} cmd_probe_t;
static cmd_probe_t s_cmd_probe;

/* PC clock minus board clock (ms), estimated from the last echo; used to correct "cmd latency" */
static int64_t s_pc_clock_offset_ms;
static bool s_pc_clock_offset_known;
static int64_t s_last_vis_t_ms;
static int64_t s_night_since_ms;
static int64_t s_calib_seen_ms;
static bool s_camera_fault;
static int64_t s_max_cycle_us;      /* longest control cycle since the last status log */
static int64_t s_max_cycle_boot_us; /* longest control cycle since boot (plans/eval.md 3.2) */

static tx_box_t s_tx;
static StaticSemaphore_t s_tx_lock_buf;
static SemaphoreHandle_t s_tx_lock;
static QueueHandle_t s_cmd_queue;
static volatile bool s_mqtt_connected_evt;

static int64_t now_ms(void)
{
    return esp_timer_get_time() / 1000;
}

/* Wall clock in Unix ms (SNTP), 0 while the time is not known */
static int64_t unix_ms(void)
{
    if (!time_sync_is_valid()) {
        return 0;
    }
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (int64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

/* ================================== Clock ================================== */

static clock_info_t read_clock(void)
{
    clock_info_t clk = { 0 };
    if (!time_sync_is_valid()) {
        return clk;
    }
    const time_t now = time(NULL);
    struct tm local;
    localtime_r(&now, &local);
    clk.valid = true;
    clk.epoch_s = (int64_t)now;
    clk.minute_of_day = (uint16_t)(local.tm_hour * 60 + local.tm_min);
    clk.day_key = (int32_t)((local.tm_year + 1900) * 10000 + (local.tm_mon + 1) * 100 + local.tm_mday);
    return clk;
}

static void save_energy(const char *reason)
{
    if (!s_nvs_ok) {
        return;
    }
    const esp_err_t err = node_store_save_energy(&s_stats);
    if (err == ESP_OK) {
        s_stats_dirty = false;
        ESP_LOGI(TAG, "energy saved to NVS (%s)", reason);
    } else {
        ESP_LOGW(TAG, "energy save failed: %s", esp_err_to_name(err));
    }
}

/* Roll the hourly history and the daily counters when the clock moves on */
static void update_clock(const clock_info_t *clk)
{
    if (!clk->valid) {
        return;
    }
    if (!s_time_was_valid) {
        s_time_was_valid = true;
        s_history_due = true;
        ESP_LOGI(TAG, "clock synced: day %ld, %02u:%02u (UTC+7)", (long)clk->day_key,
                 (unsigned)(clk->minute_of_day / 60U), (unsigned)(clk->minute_of_day % 60U));
    }
    const unsigned flags = energy_stats_roll(&s_stats, clk->epoch_s, clk->day_key);
    if ((flags & ENERGY_ROLL_DAY) != 0U) {
        ESP_LOGI(TAG, "new day %ld: energy 'today' reset", (long)clk->day_key);
    }
    if (flags != 0U) {
        s_history_due = true;
        save_energy((flags & ENERGY_ROLL_DAY) != 0U ? "new day" : "new hour");
    }
}

/* ================================== Vision ================================== */

/**
 * Camera / model fault (plans/01 section 3): init failed or frames stopped (camera_ok), or, at night,
 * no result for APP_VISION_TIMEOUT_MS. The clock starts at the latest of: last result, start of the
 * night, end of the last road calibration (no result is produced while it runs).
 */
static bool camera_is_faulty(const vision_snapshot_t *vis, bool was_night, bool calibrating, int64_t t_ms)
{
    if (!vis->camera_ok) {
        return true;
    }
    if (!was_night || calibrating) {
        return false;
    }
    int64_t since = vis->t_ms;
    since = (s_night_since_ms > since) ? s_night_since_ms : since;
    since = (s_calib_seen_ms > since) ? s_calib_seen_ms : since;
    return t_ms - since > (int64_t)APP_VISION_TIMEOUT_MS;
}

/* A new frame of detections goes into the zone rule once (not once per control cycle) */
/* ========================= Stopped-object alert ========================= */

static bool is_vehicle(const detection_t *d)
{
    return strcmp(detector_label_name(d->label), "car") == 0;
}

/* Zone 1..4 of a detection centre (frame pixels), 0 = off the road and kerbs */
static uint8_t zone_of(const road_zones_t *road, float x, float y)
{
    if (road != NULL && !road_zones_contains(road, x, y)) {
        return 0U;
    }
    unsigned zone = road != NULL ? road_zones_zone_of(road, x)
                                 : (unsigned)(x * (float)APP_LAMP_COUNT / (float)APP_FRAME_WIDTH);
    zone = zone < APP_LAMP_COUNT ? zone : APP_LAMP_COUNT - 1U;
    return (uint8_t)(zone + 1U);
}

/* Unix seconds of a moment on the esp_timer ms clock, 0 when the time is not known */
static int64_t to_unix_s(int64_t at_ms, int64_t now_ms_)
{
    return time_sync_is_valid() ? (int64_t)time(NULL) - (now_ms_ - at_ms) / 1000 : 0;
}

/* Once the alert picture is taken, send the alert again with every on-road vehicle of THAT frame: numbered left to
 * right on the dashboard, each with its zone and since when it is still (from the tracker, acknowledged
 * ones included). Runs in the control task, which owns the tracker. */
static void add_alert_objects(const vision_snapshot_t *vis, int64_t t_ms)
{
    static detection_result_t det; /* control task only */
    if (s_objs_pending_id == 0U || !vision_get_alert_det(s_objs_pending_id, &det)) {
        return;
    }
    s_objs_pending_id = 0U;
    const road_zones_t *road = vis->road_state == VISION_ROAD_FOUND ? &vis->road : NULL;
    sl_alert_t *a = &s_open_alert;
    int64_t alerted_ms = 0;
    if (stop_detect_still_since(&s_stop, a->x * (float)APP_FRAME_WIDTH, a->y * (float)APP_FRAME_HEIGHT, true,
                                &alerted_ms)) {
        a->still_s = (uint32_t)((t_ms - alerted_ms) / 1000); /* up to date for this second send */
    }
    a->obj_count = 0U;
    for (uint32_t i = 0; i < det.count && a->obj_count < SL_ALERT_MAX_OBJS; i++) {
        const detection_t *d = &det.items[i];
        if (!is_vehicle(d)) {
            continue;
        }
        const float x = d->cx * (float)APP_FRAME_WIDTH;
        const float y = d->cy * (float)APP_FRAME_HEIGHT;
        const uint8_t zone = zone_of(road, x, y);
        if (zone == 0U) {
            continue; /* off the road and kerbs: no dot, no number on the dashboard picture */
        }
        int64_t still_ms = 0;
        const bool followed = stop_detect_still_since(&s_stop, x, y, true, &still_ms);
        sl_alert_obj_t o = {
            .x = d->cx,
            .y = d->cy,
            .vehicle = true,
            .score = (uint8_t)lroundf(d->score * 100.0f),
            .zone = zone,
            .since = followed ? to_unix_s(still_ms, t_ms) : 0,
            .has_still = followed,
            .still_s = followed ? (uint32_t)((t_ms - still_ms) / 1000) : 0U, /* works without SNTP */
        };
        /* insertion sort by x: car 1 is the leftmost, the numbers stay stable for one picture */
        unsigned k = a->obj_count;
        while (k > 0U && a->objs[k - 1U].x > o.x) {
            a->objs[k] = a->objs[k - 1U];
            k--;
        }
        a->objs[k] = o;
        a->obj_count++;
    }
    queue_alert(a); /* same id: the dashboard only updates the dots and the list */
    LOGI_IN(LOG_ALERT, "alert picture: %u vehicle(s) numbered left to right", (unsigned)a->obj_count);
}

/* Open a vat_dung alert: the numbers now, the picture of the next frame as soon as vision has it,
 * then (add_alert_objects) the list of every vehicle in that picture. */
static void raise_stop_alert(const stop_evt_t *e, int64_t t_ms)
{
    const bool time_ok = time_sync_is_valid();
    const int64_t now_s = time_ok ? (int64_t)time(NULL) : 0;
    s_stop_alert_seq++;
    const uint32_t id = time_ok ? (uint32_t)now_s : s_stop_alert_seq; /* unique per event, never 0 */
    const sl_alert_t alert = {
        .id = id != 0U ? id : 1U,
        .type = "vat_dung",
        .zone = e->obj.zone,
        .cls = e->obj.vehicle ? "xe" : "nguoi",
        .since = time_ok ? now_s - (t_ms - e->still_since_ms) / 1000 : 0,
        .status = "open",
        .has_pos = true,
        .x = e->obj.x / (float)APP_FRAME_WIDTH,
        .y = e->obj.y / (float)APP_FRAME_HEIGHT,
        .score = e->obj.score,
        .still_s = (uint32_t)((t_ms - e->still_since_ms) / 1000), /* works without SNTP */
    };
    s_open_alert = alert;
    s_objs_pending_id = alert.id;
    queue_alert(&alert);
    vision_request_alert_image(alert.id);
    LOGW_IN(LOG_ALERT, "%s stopped in zone %u for %lld s (%u%%) -> alert + picture", alert.cls, (unsigned)alert.zone,
             (long long)((t_ms - e->still_since_ms) / 1000), (unsigned)alert.score);
}

static void close_stop_alert(const char *why)
{
    s_objs_pending_id = 0U;
    queue_alert_clear();
    ESP_LOGI(TAG, "stopped-object alert closed: %s", why);
    if (s_lux_check.suspect) {
        const clock_info_t clk = read_clock();
        on_lux_suspect_changed(true, &clk); /* one alert slot: bring the BH1750 one back */
    }
}

/* Feed the stopped-object tracker with the detections on the road (or kerb) of a new frame */
static void update_stop_alert(const vision_snapshot_t *vis, int64_t t_ms)
{
    if (s_node.config.stop_alert == 0U) {
        return; /* switched off: apply_config_command() closed any open alert */
    }
    const road_zones_t *road = vis->road_state == VISION_ROAD_FOUND ? &vis->road : NULL;
    stop_obj_t objs[DETECTOR_MAX_OBJECTS];
    unsigned count = 0;
    for (uint32_t i = 0; i < vis->det.count && count < DETECTOR_MAX_OBJECTS; i++) {
        const detection_t *d = &vis->det.items[i];
        const float x = d->cx * (float)APP_FRAME_WIDTH;
        const float y = d->cy * (float)APP_FRAME_HEIGHT;
        if (!is_vehicle(d)) {
            continue; /* only vehicles: a person standing still (waiting, talking) is no road incident */
        }
        const uint8_t zone = zone_of(road, x, y);
        if (zone == 0U) {
            continue; /* off the road and kerbs: parked on private ground, not a road event */
        }
        objs[count] = (stop_obj_t){
            .x = x,
            .y = y,
            .vehicle = true,
            .score = (uint8_t)lroundf(d->score * 100.0f),
            .zone = zone,
        };
        count++;
    }
    const uint32_t stop_ms = (uint32_t)s_node.config.stop_time_s * 1000U;
    const stop_evt_t e = stop_detect_update(&s_stop, objs, count, t_ms, stop_ms, APP_STOP_EPS_PX);
    if (e.type == STOP_EVT_RAISE) {
        raise_stop_alert(&e, t_ms);
    } else if (e.type == STOP_EVT_CLEAR) {
        close_stop_alert("the object left or moved on");
    }
    add_alert_objects(vis, t_ms);
}

/* Remember the frame that lit lamps which were at the base level (only the first one until it is logged) */
static void note_latency_start(const vision_snapshot_t *vis, int64_t t_ms, const uint8_t lit_before[],
                               uint8_t objects)
{
    uint8_t lit_after[APP_LAMP_COUNT];
    zone_rule_duty(&s_rule, t_ms, 0U, 1U, lit_after);
    uint8_t newly = 0U;
    for (unsigned k = 0; k < APP_LAMP_COUNT; k++) {
        if (lit_after[k] != 0U && lit_before[k] == 0U) {
            newly = (uint8_t)(newly | (1U << k));
        }
    }
    if (!APP_EVAL_LOGS || newly == 0U || s_latency.pending) {
        return;
    }
    s_latency = (latency_probe_t){
        .pending = true,
        .lamps = newly,
        .objects = objects,
        .frame_ms = vis->frame_ms,
        .result_ms = vis->t_ms,
        .seen_ms = t_ms,
    };
}

/* Called right after the PWM targets were set: log how long each stage took for the noted frame */
static void log_latency(const uint8_t duty[], bool zone_mode)
{
    if (!APP_EVAL_LOGS || !s_latency.pending) {
        return;
    }
    s_latency.pending = false;
    if (!zone_mode) {
        return; /* manual / calibration / schedule took over: the zone rule did not drive the lamps */
    }
    char lamps[2U * APP_LAMP_COUNT + 1U] = "";
    size_t n = 0;
    for (unsigned k = 0; k < APP_LAMP_COUNT; k++) {
        if ((s_latency.lamps & (1U << k)) != 0U && duty[k] == APP_FULL_PCT) {
            n += (size_t)snprintf(lamps + n, sizeof(lamps) - n, "%s%u", n > 0U ? "," : "", k + 1U);
        }
    }
    if (n == 0U) {
        return;
    }
    const int64_t cmd_ms = now_ms();
    LOGI_IN(LOG_CONTROL,
             "latency: lamp %s on, %u object(s) | FOMO %lld ms + wait control %lld ms + control->PWM %lld ms"
             " = %lld ms from frame (+ fade-up %u ms, + up to 1 frame before capture)",
             lamps, (unsigned)s_latency.objects, (long long)(s_latency.result_ms - s_latency.frame_ms),
             (long long)(s_latency.seen_ms - s_latency.result_ms), (long long)(cmd_ms - s_latency.seen_ms),
             (long long)(cmd_ms - s_latency.frame_ms), (unsigned)s_node.config.fade_up_ms);
}

static void update_vision(const vision_snapshot_t *vis, int64_t t_ms)
{
    if (!s_node.night) {
        zone_rule_reset(&s_rule);
        memset(&s_vis_stats, 0, sizeof(s_vis_stats));
        stop_detect_init(&s_stop); /* FOMO sleeps by day; an open alert stays until "Đã xử lý" */
        s_last_vis_t_ms = vis->t_ms;
        return;
    }
    if (vis->t_ms == s_last_vis_t_ms) {
        return;
    }
    s_last_vis_t_ms = vis->t_ms;
    zone_obj_t objs[DETECTOR_MAX_OBJECTS];
    unsigned count = 0;
    for (uint32_t i = 0; i < vis->det.count && count < DETECTOR_MAX_OBJECTS; i++) {
        const detection_t *d = &vis->det.items[i];
        objs[count].x = d->cx * (float)APP_FRAME_WIDTH;  /* detections are 0..1 of the frame */
        objs[count].y = d->cy * (float)APP_FRAME_HEIGHT;
        objs[count].vehicle = strcmp(detector_label_name(d->label), "car") == 0;
        count++;
    }
    uint8_t lit_before[APP_LAMP_COUNT];
    zone_rule_duty(&s_rule, t_ms, 0U, 1U, lit_before); /* 1 = lamp held bright by the zone rule */
    zone_rule_feed(&s_rule, objs, count, vis->road_state == VISION_ROAD_FOUND ? &vis->road : NULL,
                   APP_FRAME_WIDTH, t_ms, &s_vis_stats);
    note_latency_start(vis, t_ms, lit_before, (uint8_t)count);
    update_stop_alert(vis, t_ms);
}

/* ================================ Lighting ================================ */

/* Night base level from the config (set on the dashboard), clamped again in case NVS held junk */
static uint8_t base_level(void)
{
    const uint16_t pct = s_node.config.base_pct;
    if (pct < SL_BASE_MIN_PCT) {
        return SL_BASE_MIN_PCT;
    }
    return pct > SL_BASE_MAX_PCT ? SL_BASE_MAX_PCT : (uint8_t)pct;
}

/* BH1750 suspect (dark by day for APP_LUX_SUSPECT_MS) raised or cleared: tell the dashboard */
static void on_lux_suspect_changed(bool suspect, const clock_info_t *clk)
{
    if (!suspect) {
        ESP_LOGI(TAG, "BH1750 sees light again -> trusted again, alert closed");
        queue_alert_clear();
        return;
    }
    const unsigned minutes = (unsigned)(APP_LUX_SUSPECT_MS / 60000U);
    ESP_LOGW(TAG, "BH1750 dark (lux=%.1f) for %u min by day -> covered or stuck? schedule mode + alert",
             (double)s_snap.lux, minutes);
    sl_alert_t alert = {
        .id = (uint32_t)clk->epoch_s, /* unique enough: one alert per event */
        .type = "bh1750",
        .since = clk->epoch_s - (int64_t)(APP_LUX_SUSPECT_MS / 1000U),
        .status = "open",
    };
    char msg[sizeof(s_tx.alert_msg)];
    snprintf(msg, sizeof(msg),
             "Ban ngày nhưng cảm biến ánh sáng báo tối %u phút (%.0f lux): bị che hoặc hỏng? "
             "Đang chạy theo giờ, ban ngày tắt đèn",
             minutes, (double)s_snap.lux);
    alert.msg = msg; /* copied by queue_alert */
    queue_alert(&alert);
}

/* Called right after the PWM targets were set: web -> board -> PWM time of the last lamp command */
static void log_cmd_latency(const uint8_t duty[])
{
    if (!APP_EVAL_LOGS || !s_cmd_probe.pending) {
        return;
    }
    s_cmd_probe.pending = false;
    if (!s_node.manual || duty[s_cmd_probe.lamp] != s_cmd_probe.duty) {
        return; /* overridden (calibration, manual mode left): this command did not drive the lamp */
    }
    const int64_t pwm_unix_ms = unix_ms();
    if (s_cmd_probe.rx_unix_ms == 0 || pwm_unix_ms == 0) {
        LOGI_IN(LOG_SYSTEM, "cmd latency: lamp %u -> %u%% | no SNTP time, cannot measure",
                 s_cmd_probe.lamp + 1U, (unsigned)s_cmd_probe.duty);
        return;
    }
    const int64_t board_ms = pwm_unix_ms - s_cmd_probe.rx_unix_ms;
    if (s_cmd_probe.sent_unix_ms == 0) {
        LOGI_IN(LOG_SYSTEM, "cmd latency: lamp %u -> %u%% | board->PWM %lld ms (dashboard sent no time)",
                 s_cmd_probe.lamp + 1U, (unsigned)s_cmd_probe.duty, (long long)board_ms);
        return;
    }
    const int64_t raw_web_ms = s_cmd_probe.rx_unix_ms - s_cmd_probe.sent_unix_ms;
    /* PC clock behind the board (offset < 0) makes raw_web_ms too long: add the offset back */
    const int64_t web_ms = raw_web_ms + (s_pc_clock_offset_known ? s_pc_clock_offset_ms : 0);
    LOGI_IN(LOG_SYSTEM,
             "cmd latency: lamp %u -> %u%% | web->board %lld ms + board->PWM %lld ms = %lld ms (+ fade %u ms)"
             " | raw web->board %lld ms, PC clock %s",
             s_cmd_probe.lamp + 1U, (unsigned)s_cmd_probe.duty, (long long)web_ms, (long long)board_ms,
             (long long)(web_ms + board_ms), (unsigned)s_node.config.fade_up_ms, (long long)raw_web_ms,
             s_pc_clock_offset_known ? "corrected" : "NOT corrected yet (wait for a web latency line)");
}

static void update_lighting(const clock_info_t *clk, int64_t t_ms, const vision_snapshot_t *vis, bool calibrating)
{
    const lighting_input_t in = {
        .now_ms = t_ms,
        .lux_ok = s_snap.lux_ok,
        .lux = s_snap.lux,
        .time_valid = clk->valid,
        .minute_of_day = clk->minute_of_day,
        .cfg = &s_node.config,
    };
    const bool sensor_night = daynight_update(&s_daynight, &in, APP_DAY_NIGHT_HOLD_MS);
    const bool clock_night = !clk->valid || lighting_in_night_window(clk->minute_of_day, &s_node.config);

    if (calibrating) {
        s_calib_seen_ms = t_ms;
    }
    s_camera_fault = camera_is_faulty(vis, s_node.night, calibrating, t_ms);

    /* A BH1750 that answers but reads "dark" all day long is not trusted either (lux_check.h) */
    const bool was_suspect = s_lux_check.suspect;
    const bool lux_suspect = lux_check_update(&s_lux_check, &in, APP_LUX_SUSPECT_MS, APP_LUX_RECOVER_MS);
    if (lux_suspect != was_suspect) {
        on_lux_suspect_changed(lux_suspect, clk);
    }
    const bool lux_trusted = s_snap.lux_ok && !lux_suspect;

    /* Day or night: day by the SNTP clock is always day (a dark BH1750 then only raises the alert);
     * otherwise the BH1750 when it is trusted, else the clock (night when the time is unknown).
     * A camera fault does not change this: the light sensor still tells the truth. */
    const bool was_night = s_node.night;
    s_node.night = lux_check_resolve_night(s_node.force_night, lux_trusted, sensor_night, clk->valid, clock_night);
    if (s_node.night && !was_night) {
        s_night_since_ms = t_ms;
    }

    if (s_node.manual && t_ms >= s_node.manual_until_ms) {
        s_node.manual = false;
        ESP_LOGI(TAG, "manual mode timed out -> auto");
    }

    uint8_t duty[APP_LAMP_COUNT];
    if (calibrating) {
        memset(duty, APP_FULL_PCT, sizeof(duty)); /* the road calibration needs every lamp at full level */
    } else if (s_node.manual) {
        s_node.mode = SL_MODE_MANUAL;
        memcpy(duty, s_node.manual_duty, sizeof(duty));
    } else {
        /* Without the camera nobody can tell where a person is, so at night every lamp is full (schedule
         * mode, plans/01). With the camera a BH1750 fault changes nothing for the lamps: base level plus
         * the zones of the detections (auto mode). */
        lighting_input_t decide_in = in;
        decide_in.lux_ok = !s_camera_fault;
        const uint8_t base = base_level();
        const lighting_decision_t d = lighting_decide(&decide_in, s_node.night, base, APP_FULL_PCT);
        s_node.mode = d.mode;
        memset(duty, d.duty, sizeof(duty));
        if (d.mode == SL_MODE_AUTO && s_node.night) {
            zone_rule_duty(&s_rule, t_ms, base, APP_FULL_PCT, duty); /* per lamp, by zone */
        }
    }
    for (unsigned k = 0; k < APP_LAMP_COUNT; k++) {
        (void)lamp_pwm_set(k, duty[k], s_node.config.fade_up_ms, s_node.config.fade_down_ms);
    }
    lamp_pwm_service();
    log_latency(duty, !calibrating && !s_node.manual && s_node.mode == SL_MODE_AUTO && s_node.night);
    log_cmd_latency(duty);
}

/* ================================= Energy ================================= */

static void update_energy(int64_t dt_us)
{
    for (unsigned k = 0; k < APP_LAMP_COUNT; k++) {
        if (!s_snap.ina_ok[k]) {
            energy_acc_skip(&s_energy_acc, k);
            continue;
        }
        const double mwh = energy_acc_add(&s_energy_acc, k, s_snap.ina[k].power_mw, s_snap.timestamp_us);
        if (mwh > 0.0) {
            energy_stats_add(&s_stats, k, mwh, s_node.night);
            s_stats_dirty = true;
        }
    }
    if (s_node.night && dt_us > 0 && dt_us <= ENERGY_ACC_MAX_GAP_US) {
        energy_stats_add_night_time(&s_stats, (double)dt_us / 1e6);
    }
}

/* Self-calibrate P100: average power of a lamp held at a steady 100 % */
static void update_p100(int64_t t_ms)
{
    for (unsigned k = 0; k < APP_LAMP_COUNT; k++) {
        p100_cal_t *c = &s_p100[k];
        const bool steady_full = lamp_pwm_target(k) == APP_FULL_PCT && lamp_pwm_is_steady(k)
                                 && s_snap.ina_ok[k];
        if (!steady_full) {
            c->since_ms = -1;
            continue;
        }
        if (c->since_ms < 0) {
            *c = (p100_cal_t){ .since_ms = t_ms };
            continue;
        }
        c->sum_mw += (double)s_snap.ina[k].power_mw;
        c->samples++;
        if (t_ms - c->since_ms < (int64_t)APP_P100_WINDOW_MS) {
            continue;
        }
        const float avg = (float)(c->sum_mw / (double)c->samples);
        const float old = s_stats.p100_mw[k];
        if (avg > 0.0f && (old <= 0.0f || fabsf(avg - old) > APP_P100_CHANGE_RATIO * old)) {
            s_stats.p100_mw[k] = avg;
            ESP_LOGI(TAG, "lamp %u: P100 calibrated = %.1f mW", k + 1U, (double)avg);
            save_energy("P100");
        }
        *c = (p100_cal_t){ .since_ms = t_ms };
    }
}

#define INA_ZERO_LOG_STEP_MA 0.05f /* log a re-estimated offset only when it moved this much */

/* Remove each INA219's zero offset (ina_zero.h) right after the read, before anything uses the values.
 * A lamp is "off" when commanded to 0 % and not fading; the reading taken then is pure offset. */
static void correct_ina_offsets(int64_t t_ms)
{
    for (unsigned k = 0; k < APP_LAMP_COUNT; k++) {
        ina219_sample_t *s = &s_snap.ina[k];
        ina_zero_t *z = &s_ina_zero[k];
        const bool lamp_off = lamp_pwm_target(k) == 0U && lamp_pwm_is_steady(k);
        const bool was_valid = z->valid;
        const float before = z->offset_ma;
        if (ina_zero_update(z, s_snap.ina_ok[k], s->current_ma, lamp_off, t_ms)
            && (!was_valid || fabsf(z->offset_ma - before) >= INA_ZERO_LOG_STEP_MA)) {
            ESP_LOGI(TAG, "lamp %u: INA219 zero offset %.3f mA", k + 1U, (double)z->offset_ma);
        }
        if (s_snap.ina_ok[k]) {
            s->current_ma = ina_zero_correct(z, s->current_ma);
            s->power_mw = s->bus_v * s->current_ma;
        }
    }
}

/* Lamp faults from the INA219 readings (lamp_fault.h): judged here, not in the dashboard, because
 * only the board knows when a lamp is fading and what it draws at 100 % (P100). */
static void update_faults(int64_t t_ms)
{
    for (unsigned k = 0; k < APP_LAMP_COUNT; k++) {
        const ina219_sample_t *s = &s_snap.ina[k];
        const lamp_fault_in_t in = {
            .ina_ok = s_snap.ina_ok[k],
            .bus_v = s->bus_v,
            .current_ma = s->current_ma,
            .power_mw = s->power_mw,
            .duty = lamp_pwm_target(k),
            .steady = lamp_pwm_is_steady(k),
            .p100_mw = s_stats.p100_mw[k],
            .now_ms = t_ms,
        };
        const lamp_fault_t before = s_fault[k].reported;
        const lamp_fault_t now = lamp_fault_update(&s_fault[k], &in);
        if (now != before) {
            LOGW_IN(LOG_ALERT, "lamp %u: fault %d -> %d (duty %u%%, %.2f mA, %.1f mW, P100 %.1f mW)", k + 1U,
                     (int)before, (int)now, (unsigned)in.duty, (double)in.current_ma, (double)in.power_mw,
                     (double)in.p100_mw);
        }
    }
}

/* ================================== Log ================================== */

static const char *reset_reason_text(esp_reset_reason_t r)
{
    switch (r) {
    case ESP_RST_POWERON: return "power-on";
    case ESP_RST_SW: return "software restart";
    case ESP_RST_PANIC: return "PANIC (crash)";
    case ESP_RST_INT_WDT: return "interrupt watchdog";
    case ESP_RST_TASK_WDT: return "task watchdog";
    case ESP_RST_WDT: return "other watchdog";
    case ESP_RST_BROWNOUT: return "BROWNOUT (supply too weak)";
    case ESP_RST_DEEPSLEEP: return "deep sleep";
    case ESP_RST_EXT: return "external pin";
    default: return "other";
    }
}

/* Once a minute: uptime + free memory, for the long run test (plans/eval.md 6.1, 6.2). A free heap that
 * keeps going down over hours means a leak. */
static void log_health(void)
{
    static unsigned s_calls;
    if (!APP_EVAL_LOGS || s_calls++ % (60000U / APP_LOG_PERIOD_MS) != 0U || !log_view_show(LOG_SYSTEM)) {
        return;
    }
    const unsigned kb = 1024U;
    ESP_LOGI(TAG,
             "health: up %lld s, last reset: %s | internal RAM free %u KB (min %u KB) of %u KB"
             " | PSRAM free %u KB (min %u KB) of %u KB",
             (long long)(esp_timer_get_time() / 1000000), reset_reason_text(esp_reset_reason()),
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / kb),
             (unsigned)(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL) / kb),
             (unsigned)(heap_caps_get_total_size(MALLOC_CAP_INTERNAL) / kb),
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / kb),
             (unsigned)(heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM) / kb),
             (unsigned)(heap_caps_get_total_size(MALLOC_CAP_SPIRAM) / kb));
}

static void log_status(const clock_info_t *clk, const vision_snapshot_t *vis)
{
    if (clk->valid) {
        LOGI_IN(LOG_CONTROL, "%02u:%02u | mode=%s | %s%s", (unsigned)(clk->minute_of_day / 60U),
                 (unsigned)(clk->minute_of_day % 60U), MODE_TEXT[s_node.mode],
                 s_node.night ? "NIGHT" : "DAY", s_node.force_night ? " (forced)" : "");
    } else {
        LOGI_IN(LOG_CONTROL, "--:-- (no SNTP) | mode=%s | %s%s", MODE_TEXT[s_node.mode],
                 s_node.night ? "NIGHT" : "DAY", s_node.force_night ? " (forced)" : "");
    }
    LOGI_IN(LOG_CONTROL, "control cycle max %lld us (since boot %lld us) of %u ms", (long long)s_max_cycle_us,
             (long long)s_max_cycle_boot_us, (unsigned)APP_SAMPLE_PERIOD_MS);
    s_max_cycle_us = 0;
    log_health();
    if (s_camera_fault) {
        LOGW_IN(LOG_ALERT, "camera/model fault -> no zones: night lamps full, day lamps off");
    } else if (log_view_show(LOG_AI) || log_view_show(LOG_ZONE)) {
        ESP_LOGI(TAG, "vision: %.1f fps, road %s, %u person(s) in zones 0x%X, %u vehicle(s) in zones 0x%X",
                 (double)vis->fps, ROAD_TEXT[vis->road_state], (unsigned)s_vis_stats.persons,
                 (unsigned)s_vis_stats.person_zone_mask, (unsigned)s_vis_stats.vehicles,
                 (unsigned)s_vis_stats.vehicle_zone_mask);
    }
    if (s_snap.lux_ok && s_lux_check.suspect) {
        LOGW_IN(LOG_ALERT, "BH1750     lux=%.1f, dark by day -> not trusted, day/night from the clock",
                 (double)s_snap.lux);
    } else if (s_snap.lux_ok) {
        LOGI_IN(LOG_ALERT, "BH1750     lux=%.1f", (double)s_snap.lux);
    } else {
        LOGW_IN(LOG_ALERT, "BH1750     offline -> day/night from the clock%s", s_camera_fault ? "" : " (camera zones still work)");
    }
    for (unsigned k = 0; k < APP_LAMP_COUNT && (log_view_show(LOG_CONTROL) || log_view_show(LOG_ENERGY)); k++) {
        const unsigned duty = lamp_pwm_get(k);
        if (!s_snap.ina_ok[k]) {
            ESP_LOGW(TAG, "Lamp %u  duty=%3u%%  INA219 offline", k + 1U, duty);
            continue;
        }
        const ina219_sample_t *s = &s_snap.ina[k];
        ESP_LOGI(TAG, "Lamp %u  duty=%3u%%  V=%.3f V  I=%6.2f mA  P=%6.1f mW  today=%.1f total=%.1f mWh",
                 k + 1U, duty, (double)s->bus_v, (double)s->current_ma, (double)s->power_mw,
                 s_stats.today_mwh[k], s_stats.total_mwh[k]);
    }
    LOGI_IN(LOG_SYSTEM, "WiFi %s | MQTT %s", sl_wifi_is_connected() ? "up" : "down",
             sl_mqtt_is_connected() ? "connected" : "not connected");
}

/* ============================ Control -> network ============================ */

static bool publish_ok(const char *what, esp_err_t err)
{
    if (err == ESP_OK) {
        return true;
    }
    if (err != ESP_ERR_INVALID_STATE) { /* INVALID_STATE = not connected, expected */
        ESP_LOGW(TAG, "publish %s failed: %s", what, esp_err_to_name(err));
    }
    return false;
}

/* Control task: build the state + energy message and leave it for the network task */
static void queue_telemetry(const vision_snapshot_t *vis)
{
    uint8_t duty[APP_LAMP_COUNT];
    for (unsigned k = 0; k < APP_LAMP_COUNT; k++) {
        duty[k] = lamp_pwm_get(k);
    }
    /* Every active sensor fault, comma separated: "bh1750" = no answer on I2C, "bh1750_dark" = answers but
     * reads dark by day (covered/stuck, lux_check.h), "camera" = camera/model. String literals only: the
     * network task encodes s_tx.state after this function returns, so no stack buffer may be pointed to. */
    static const char *const k_sensor_errors[3][2] = {
        /* camera ok        camera fault */
        { NULL,             "camera" },             /* BH1750 fine */
        { "bh1750",         "bh1750,camera" },      /* BH1750 offline */
        { "bh1750_dark",    "bh1750_dark,camera" }, /* BH1750 dark by day */
    };
    const unsigned lux_err = !s_snap.lux_ok ? 1U : (s_lux_check.suspect ? 2U : 0U);
    const char *sensor_error = k_sensor_errors[lux_err][s_camera_fault ? 1 : 0];
    const sl_state_t state = {
        .night = s_node.night,
        .lux = s_snap.lux_ok ? (int32_t)lroundf(s_snap.lux) : 0,
        .mode = s_node.mode,
        .force_night = s_node.force_night,
        .duty = { duty[0], duty[1], duty[2], duty[3] },
        .persons = s_vis_stats.persons,
        .vehicles = s_vis_stats.vehicles,
        .zone_mask = s_vis_stats.zone_mask,
        .fps = vis->fps,
        .sensor_error = sensor_error,
    };

    sl_lamp_energy_t lamps[SL_LAMP_COUNT];
    for (unsigned k = 0; k < SL_LAMP_COUNT; k++) {
        const ina219_sample_t *s = &s_snap.ina[k];
        lamps[k] = (sl_lamp_energy_t){
            .duty = duty[k],
            .ina_ok = s_snap.ina_ok[k],
            .v_volt = s->bus_v,
            .i_ma = s->current_ma,
            .p_mw = s->power_mw,
            .e_today_mwh = (uint32_t)llround(s_stats.today_mwh[k]),
            .e_total_mwh = (uint32_t)llround(s_stats.total_mwh[k]),
            .saving_pct = energy_stats_saving_pct(&s_stats, k),
            .fault = (int8_t)s_fault[k].reported, /* lamp_fault.h codes = dashboard FAULT_TEXT */
        };
    }
    if (xSemaphoreTake(s_tx_lock, pdMS_TO_TICKS(TX_LOCK_MS)) != pdTRUE) {
        return; /* the network task is copying: the next cycle follows */
    }
    s_tx.state = state;
    memcpy(s_tx.lamps, lamps, sizeof(lamps));
    s_tx.telemetry_ready = true;
    xSemaphoreGive(s_tx_lock);
}

static void queue_config(void)
{
    if (xSemaphoreTake(s_tx_lock, pdMS_TO_TICKS(TX_LOCK_MS)) == pdTRUE) {
        s_tx.config = s_node.config;
        s_tx.config_ready = true;
        xSemaphoreGive(s_tx_lock);
    }
}

static void queue_history(void)
{
    if (s_stats.hist_hour < 0) {
        return; /* no clock yet: hours are unknown */
    }
    if (xSemaphoreTake(s_tx_lock, pdMS_TO_TICKS(TX_LOCK_MS)) == pdTRUE) {
        energy_stats_history(&s_stats, s_tx.history);
        s_tx.history_t_sec = s_stats.hist_hour * 3600;
        s_tx.history_ready = true;
        s_history_due = false;
        xSemaphoreGive(s_tx_lock);
    }
}

static void queue_alert_clear(void)
{
    if (xSemaphoreTake(s_tx_lock, pdMS_TO_TICKS(TX_LOCK_MS)) == pdTRUE) {
        s_tx.alert_clear = true;
        s_tx.alert_ready = false; /* the newest request wins */
        s_tx.alert_image_id = 0U; /* a picture still on its way must not follow the clear */
        xSemaphoreGive(s_tx_lock);
    }
}

/* Hand an alert (retained on streetlight/alert) to the network task; alert->msg is copied */
static void queue_alert(const sl_alert_t *alert)
{
    if (xSemaphoreTake(s_tx_lock, pdMS_TO_TICKS(TX_LOCK_MS)) != pdTRUE) {
        ESP_LOGW(TAG, "alert dropped: TX box busy");
        return;
    }
    s_tx.alert = *alert;
    snprintf(s_tx.alert_msg, sizeof(s_tx.alert_msg), "%s", alert->msg != NULL ? alert->msg : "");
    s_tx.alert.msg = alert->msg != NULL ? s_tx.alert_msg : NULL;
    s_tx.alert_image_id = alert->has_pos ? alert->id : 0U; /* only vat_dung alerts have a picture */
    s_tx.alert_ready = true;
    s_tx.alert_clear = false;
    xSemaphoreGive(s_tx_lock);
}

static void apply_config_command(const sl_config_patch_t *patch)
{
    sl_config_t next;
    sl_codec_apply_config(&s_node.config, patch, &next);
    if (memcmp(&next, &s_node.config, sizeof(next)) != 0) {
        s_node.config = next;
        /* Write NVS only when something really changed (flash wear) */
        if (s_nvs_ok) {
            const esp_err_t err = node_store_save_config(&s_node.config);
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "config save failed: %s", esp_err_to_name(err));
            }
        }
    }
    if (s_node.config.stop_alert == 0U) {
        if (s_stop.alert_active) {
            close_stop_alert("stopped-object alert switched off");
        }
        stop_detect_init(&s_stop);
    }
    ESP_LOGI(TAG, "config: lux %u/%u, fade %u/%u ms, base %u%%, stop alert %s after %u s, night %s-%s",
             (unsigned)s_node.config.lux_night, (unsigned)s_node.config.lux_day,
             (unsigned)s_node.config.fade_up_ms, (unsigned)s_node.config.fade_down_ms,
             (unsigned)s_node.config.base_pct, s_node.config.stop_alert != 0U ? "on" : "off",
             (unsigned)s_node.config.stop_time_s, s_node.config.night_start, s_node.config.night_end);
    queue_config(); /* always echo: the dashboard shows the values the board really uses */
}

/* plans/eval.md 6.4: the dashboard got a state message and echoed its time back.
 * round trip uses only the board clock (exact); the one-way parts include the PC/board clock difference.
 * With equal way there and back: measured board->web = d + off, web->board = d - off, so
 * off = (board->web - web->board) / 2 and d = round trip / 2. */
static void log_web_latency(const sl_cmd_t *cmd)
{
    if (!APP_EVAL_LOGS || cmd->rx_unix_ms == 0) {
        return;
    }
    const int64_t sent = cmd->u.latency.board_ms;
    const int64_t web = cmd->u.latency.web_ms;
    const int64_t to_web = web - sent;
    const int64_t to_board = cmd->rx_unix_ms - web;
    s_pc_clock_offset_ms = (to_web - to_board) / 2;
    s_pc_clock_offset_known = true;
    LOGI_IN(LOG_SYSTEM, "web latency: board->web %lld ms, web->board %lld ms, round trip %lld ms (exact)"
             " -> one way ~%lld ms, PC clock %+lld ms vs board",
             (long long)to_web, (long long)to_board, (long long)(cmd->rx_unix_ms - sent),
             (long long)((cmd->rx_unix_ms - sent) / 2), (long long)s_pc_clock_offset_ms);
}

static void handle_command(const sl_cmd_t *cmd)
{
    const int64_t t_ms = now_ms();
    switch (cmd->type) {
    case SL_CMD_MODE:
        if (cmd->u.mode == SL_MODE_MANUAL) {
            /* Start manual mode from what the lamps are doing now */
            for (unsigned k = 0; k < APP_LAMP_COUNT; k++) {
                s_node.manual_duty[k] = lamp_pwm_target(k);
            }
            s_node.manual = true;
            s_node.manual_until_ms = t_ms + (int64_t)APP_MANUAL_TIMEOUT_MS;
        } else {
            s_node.manual = false;
        }
        ESP_LOGI(TAG, "cmd: mode -> %s", s_node.manual ? "manual" : "auto");
        break;
    case SL_CMD_LAMP:
        if (!s_node.manual) {
            ESP_LOGW(TAG, "cmd: lamp %u ignored, switch to manual mode first",
                     (unsigned)cmd->u.lamp.lamp);
            break;
        }
        s_node.manual_duty[cmd->u.lamp.lamp - 1U] = cmd->u.lamp.duty;
        s_node.manual_until_ms = t_ms + (int64_t)APP_MANUAL_TIMEOUT_MS;
        LOGI_IN(LOG_SYSTEM, "cmd: lamp %u -> %u%%", (unsigned)cmd->u.lamp.lamp, (unsigned)cmd->u.lamp.duty);
        s_cmd_probe = (cmd_probe_t){
            .pending = true,
            .lamp = (uint8_t)(cmd->u.lamp.lamp - 1U),
            .duty = cmd->u.lamp.duty,
            .sent_unix_ms = cmd->sent_unix_ms,
            .rx_unix_ms = cmd->rx_unix_ms,
        };
        break;
    case SL_CMD_FORCE_NIGHT:
        s_node.force_night = cmd->u.force_night;
        ESP_LOGI(TAG, "cmd: force_night -> %s", s_node.force_night ? "true" : "false");
        break;
    case SL_CMD_ALERT_ACK:
        ESP_LOGI(TAG, "cmd: alert '%s' acknowledged", cmd->u.alert_id);
        queue_alert_clear();
        stop_detect_ack(&s_stop); /* that stopped object is not reported again */
        break;
    case SL_CMD_CONFIG:
        apply_config_command(&cmd->u.config);
        break;
    case SL_CMD_LATENCY:
        log_web_latency(cmd);
        return; /* not a real command: no telemetry push */
    case SL_CMD_RECALIBRATE:
        /* the vision task runs it next (day or night): lamps at full ~2 s, then the picture goes to
         * streetlight/calib/image */
        vision_request_recalibration();
        ESP_LOGI(TAG, "cmd: find the road again");
        break;
    default:
        break;
    }
    s_last_telemetry_ms = 0; /* push the new state on the next cycle */
}

/* Control task: commands the network task forwarded, and the "MQTT connected" event */
static void drain_commands(void)
{
    sl_cmd_t cmd;
    while (xQueueReceive(s_cmd_queue, &cmd, 0) == pdTRUE) {
        handle_command(&cmd);
    }
    if (s_mqtt_connected_evt) {
        s_mqtt_connected_evt = false;
        ESP_LOGI(TAG, "MQTT connected, publishing config + history");
        queue_config();
        s_history_due = true;
        s_last_telemetry_ms = 0;
    }
}

/* ================================ Network task ================================ */

static void start_wifi(void)
{
    esp_err_t err = sl_secrets_load(&s_secrets);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "no WiFi/MQTT secrets in NVS -> offline mode (no SNTP, no MQTT)");
        return;
    }
    s_secrets_ok = true;
    err = sl_wifi_start(s_secrets.wifi_ssid, s_secrets.wifi_pass);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "sl_wifi_start failed: %s", esp_err_to_name(err));
    }
}

/* Start SNTP and MQTT only once WiFi has an IP */
static void start_network_services(void)
{
    if (!s_secrets_ok || !sl_wifi_is_connected()) {
        return;
    }
    (void)time_sync_start(); /* no-op after the first successful call */
    if (s_mqtt_started) {
        return;
    }
    s_mqtt_started = true; /* one attempt; esp-mqtt reconnects by itself afterwards */
    const sl_mqtt_config_t cfg = {
        .uri = s_secrets.mqtt_uri,
        .username = s_secrets.mqtt_user,
        .password = s_secrets.mqtt_pass,
    };
    const esp_err_t err = sl_mqtt_start(&cfg);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "MQTT client started -> %s", s_secrets.mqtt_uri);
    } else {
        ESP_LOGE(TAG, "sl_mqtt_start failed: %s", esp_err_to_name(err));
    }
}

/* Network task: hand MQTT events to the control task (commands through the queue) */
static void drain_mqtt_events(void)
{
    static int64_t s_down_since_ms = -1; /* reconnect time log (plans/eval.md 6.3) */
    sl_mqtt_event_t ev;
    while (sl_mqtt_wait_event(&ev, 0)) {
        switch (ev.type) {
        case SL_MQTT_EVT_CONNECTED:
            s_mqtt_connected_evt = true;
            if (APP_EVAL_LOGS && s_down_since_ms >= 0) {
                LOGI_IN(LOG_SYSTEM, "MQTT back after %lld ms offline", (long long)(now_ms() - s_down_since_ms));
                s_down_since_ms = -1;
            }
            break;
        case SL_MQTT_EVT_DISCONNECTED:
            ESP_LOGW(TAG, "MQTT disconnected");
            if (s_down_since_ms < 0) {
                s_down_since_ms = now_ms();
            }
            break;
        case SL_MQTT_EVT_COMMAND:
            if (ev.cmd.rx_unix_ms == 0) {
                ev.cmd.rx_unix_ms = unix_ms(); /* sl_mqtt stamps on arrival; fallback only */
            }
            if (xQueueSend(s_cmd_queue, &ev.cmd, 0) != pdTRUE) {
                ESP_LOGW(TAG, "command queue full: command dropped");
            }
            break;
        default:
            break;
        }
    }
}

static void clear_tx_flag(bool *flag)
{
    if (xSemaphoreTake(s_tx_lock, pdMS_TO_TICKS(TX_LOCK_MS)) == pdTRUE) {
        *flag = false;
        xSemaphoreGive(s_tx_lock);
    }
}

/* Network task: publish what the control task prepared (blocking TLS calls happen here, not there) */
static void flush_tx(void)
{
    static tx_box_t local; /* the network task is the only user: static keeps its stack small */
    if (!sl_mqtt_is_connected()) {
        return; /* the ready flags stay set until the broker is back */
    }
    if (xSemaphoreTake(s_tx_lock, pdMS_TO_TICKS(TX_LOCK_MS)) != pdTRUE) {
        return;
    }
    local = s_tx;
    xSemaphoreGive(s_tx_lock);

    if (local.config_ready && publish_ok("config", sl_mqtt_publish_config(&local.config))) {
        clear_tx_flag(&s_tx.config_ready);
    }
    if (local.alert_clear && publish_ok("alert clear", sl_mqtt_clear_alert())) {
        clear_tx_flag(&s_tx.alert_clear);
    }
    if (local.alert_ready) {
        local.alert.msg = local.alert.msg != NULL ? local.alert_msg : NULL; /* point into our own copy */
        if (publish_ok("alert", sl_mqtt_publish_alert(&local.alert))) {
            clear_tx_flag(&s_tx.alert_ready);
            LOGI_IN(LOG_ALERT, "MQTT -> streetlight/alert (%s)", local.alert.type);
        }
    }
    if (local.telemetry_ready) {
        local.state.t_unix_ms = APP_EVAL_LOGS ? unix_ms() : 0; /* 6.4 latency test: dashboard echoes it */
        const bool state_sent = publish_ok("state", sl_mqtt_publish_state(&local.state));
        const bool energy_sent = publish_ok("energy", sl_mqtt_publish_energy(local.lamps));
        if (state_sent && energy_sent) {
            clear_tx_flag(&s_tx.telemetry_ready);
            LOGI_IN(LOG_SYSTEM, "MQTT -> streetlight/state + streetlight/energy");
        }
    }
    if (local.history_ready) {
        const esp_err_t err = sl_mqtt_publish_history(local.history_t_sec,
                                                      (const uint32_t (*)[SL_HISTORY_HOURS])local.history);
        if (publish_ok("history", err)) {
            clear_tx_flag(&s_tx.history_ready);
            LOGI_IN(LOG_SYSTEM, "MQTT -> streetlight/energy/history");
        }
    }
}

/* Road calibration result -> MQTT payload; the time is shifted back to when the attempt ran */
static sl_calib_t to_sl_calib(const vision_calib_info_t *info)
{
    const road_zones_t *r = &info->road;
    sl_calib_t c = {
        .found = info->found,
        .width = APP_FRAME_WIDTH,
        .height = APP_FRAME_HEIGHT,
    };
    if (info->found) {
        c.top_a = r->top_a;
        c.top_b = r->top_b;
        c.bottom_a = r->bottom_a;
        c.bottom_b = r->bottom_b;
        c.has_left = r->has_left;
        c.left_a = r->left_a;
        c.left_b = r->left_b;
        c.has_kerb_top = r->has_kerb_top;
        c.kerb_top_a = r->kerb_top_a;
        c.kerb_top_b = r->kerb_top_b;
        c.has_kerb_bottom = r->has_kerb_bottom;
        c.kerb_bottom_a = r->kerb_bottom_a;
        c.kerb_bottom_b = r->kerb_bottom_b;
        c.x_min = r->x_min;
        c.x_max = r->x_max;
        memcpy(c.bounds, r->boundaries_x, sizeof(c.bounds));
    }
    if (time_sync_is_valid()) {
        c.t_sec = (int64_t)time(NULL) - (now_ms() - info->t_ms) / 1000;
    }
    return c;
}

_Static_assert(SL_ZONE_COUNT - 1 == ROAD_ZONES_COUNT - 1U, "zone cuts differ between road_zones and MQTT");
_Static_assert(APP_CALIB_JPEG_BYTES <= SL_MQTT_IMAGE_MAX_JPEG, "calibration JPEG larger than MQTT accepts");

/* Network task: send each new calibration attempt (picture + numbers) once, retained on the broker */
static void flush_calib(uint8_t *jpeg_copy)
{
    static uint32_t s_sent_seq;
    if (!sl_mqtt_is_connected() || vision_calib_seq() == s_sent_seq) {
        return;
    }
    vision_calib_info_t info;
    const size_t cap = (jpeg_copy != NULL) ? APP_CALIB_JPEG_BYTES : 0U; /* no copy buffer: numbers only */
    uint8_t dummy;
    if (!vision_get_calib(jpeg_copy != NULL ? jpeg_copy : &dummy, cap, &info)) {
        return;
    }
    const sl_calib_t calib = to_sl_calib(&info);
    if (publish_ok("calib", sl_mqtt_publish_calib(&calib, jpeg_copy, info.jpeg_len))) {
        s_sent_seq = info.seq;
        ESP_LOGI(TAG, "MQTT -> streetlight/calib (%s, picture %u B)", info.found ? "road found" : "road not found",
                 (unsigned)info.jpeg_len);
    }
}

/* Network task: the picture of the open vat_dung alert, once vision has encoded it (retained on alert/image).
 * The vehicle list of that picture is added to the alert by the control task (add_alert_objects).
 * jpeg_copy is the calibration copy buffer: both flush_* run one after the other in this task. */
static void flush_alert_image(uint8_t *jpeg_copy)
{
    static uint32_t s_sent_id;
    if (jpeg_copy == NULL || !sl_mqtt_is_connected()
        || xSemaphoreTake(s_tx_lock, pdMS_TO_TICKS(TX_LOCK_MS)) != pdTRUE) {
        return;
    }
    const uint32_t want = s_tx.alert_image_id;
    const bool alert_pending = s_tx.alert_ready; /* the alert numbers go first */
    xSemaphoreGive(s_tx_lock);
    if (want == 0U || want == s_sent_id || alert_pending) {
        return;
    }
    size_t len = 0U;
    if (!vision_get_alert_image(want, jpeg_copy, APP_CALIB_JPEG_BYTES, &len, NULL)) {
        return; /* not encoded yet */
    }
    if (len == 0U) {
        s_sent_id = want; /* encoding failed: the alert stays without picture */
        return;
    }
    if (publish_ok("alert image", sl_mqtt_publish_image(jpeg_copy, len))) {
        s_sent_id = want;
        LOGI_IN(LOG_ALERT, "MQTT -> streetlight/alert/image (%u B)", (unsigned)len);
    }
}

static void network_task(void *arg)
{
    (void)arg;
    /* Copy of the calibration JPEG for the TLS publish: PSRAM, allocated once, never freed */
    uint8_t *calib_jpeg = heap_caps_malloc(APP_CALIB_JPEG_BYTES, MALLOC_CAP_SPIRAM);
    if (calib_jpeg == NULL) {
        ESP_LOGW(TAG, "no PSRAM for the calibration picture: the dashboard gets the numbers only");
    }
    for (;;) {
        start_network_services();
        drain_mqtt_events();
        flush_tx();
        flush_calib(calib_jpeg);
        flush_alert_image(calib_jpeg);
        vTaskDelay(pdMS_TO_TICKS(NETWORK_PERIOD_MS));
    }
}

/* ================================ Control task ================================ */

static void control_task(void *arg)
{
    (void)arg;
    int64_t last_log_ms = 0;
    int64_t last_save_ms = now_ms();
    int64_t last_cycle_us = esp_timer_get_time();
    TickType_t last_wake = xTaskGetTickCount();

    for (;;) {
        const int64_t cycle_us = esp_timer_get_time();
        const int64_t t_ms = cycle_us / 1000;

        sensor_hub_sample(&s_snap);
        correct_ina_offsets(t_ms);
        const clock_info_t clk = read_clock();
        update_clock(&clk);

        vision_snapshot_t vis;
        vision_get(&vis);
        const bool calibrating = vision_calibrating();
        update_lighting(&clk, t_ms, &vis, calibrating);
        vision_set_night(s_node.night);
        update_vision(&vis, t_ms);
        update_energy(cycle_us - last_cycle_us);
        update_p100(t_ms);
        update_faults(t_ms);
        last_cycle_us = cycle_us;

        drain_commands();
        if (s_history_due) {
            queue_history();
        }
        if (t_ms - s_last_telemetry_ms >= (int64_t)APP_TELEMETRY_PERIOD_MS) {
            s_last_telemetry_ms = t_ms;
            queue_telemetry(&vis);
        }
        if (t_ms - last_log_ms >= (int64_t)APP_LOG_PERIOD_MS) {
            last_log_ms = t_ms;
            log_status(&clk, &vis);
        }
        if (s_stats_dirty && t_ms - last_save_ms >= (int64_t)APP_ENERGY_SAVE_PERIOD_MS) {
            last_save_ms = t_ms;
            save_energy("periodic");
        }

        const int64_t used_us = esp_timer_get_time() - cycle_us;
        s_max_cycle_us = (used_us > s_max_cycle_us) ? used_us : s_max_cycle_us;
        s_max_cycle_boot_us = (used_us > s_max_cycle_boot_us) ? used_us : s_max_cycle_boot_us;
        (void)xTaskDelayUntil(&last_wake, pdMS_TO_TICKS(APP_SAMPLE_PERIOD_MS));
    }
}

/* ================================== Init ================================== */

static void load_persistent_state(void)
{
    esp_err_t err = node_store_load_config(&s_node.config);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "config loaded from NVS");
    } else {
        ESP_LOGI(TAG, "no stored config (%s), using defaults", esp_err_to_name(err));
    }
    err = node_store_load_energy(&s_stats);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "energy loaded from NVS");
    } else {
        ESP_LOGI(TAG, "no stored energy (%s), starting from 0", esp_err_to_name(err));
    }
}

static void init_node(void)
{
    time_sync_set_timezone();
    s_node.config = SL_CONFIG_DEFAULT;
    s_node.mode = SL_MODE_AUTO;
    energy_acc_init(&s_energy_acc);
    energy_stats_init(&s_stats);
    zone_rule_reset(&s_rule);
    for (unsigned k = 0; k < APP_LAMP_COUNT; k++) {
        s_p100[k].since_ms = -1;
        lamp_fault_init(&s_fault[k]);
        ina_zero_init(&s_ina_zero[k]);
    }
    lux_check_init(&s_lux_check);
    stop_detect_init(&s_stop);

    esp_err_t err = lamp_pwm_init(LAMP_GPIOS, APP_LAMP_COUNT, APP_PWM_FREQ_HZ, APP_LAMP_ACTIVE_LOW != 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "lamp PWM init failed: %s (check APP_LAMPx_GPIO)", esp_err_to_name(err));
    }
    if (sensor_hub_init() != ESP_OK) {
        ESP_LOGE(TAG, "sensor bus not available, check APP_I2C_* in app_config.h");
    }

    /* Never erase NVS automatically: it holds the WiFi/MQTT secrets */
    err = nvs_flash_init();
    s_nvs_ok = (err == ESP_OK);
    if (s_nvs_ok) {
        load_persistent_state();
    } else {
        ESP_LOGE(TAG, "nvs_flash_init: %s -> no persistence, no network", esp_err_to_name(err));
    }

    /* Camera + model before WiFi: the frame buffers and the model arena take PSRAM first.
     * A failure is logged and reported as state.sensor_error "camera"; the node keeps running. */
    (void)vision_start();
    if (s_nvs_ok) {
        start_wifi();
    }
}

/* ================================== Main ================================== */

static void run_normal_mode(void)
{
    ESP_LOGI(TAG, "streetlight-node starting (normal mode)");
    log_view_start(); /* TEMPORARY eval aid: type 0-6 in the monitor to filter the log */
    s_tx_lock = xSemaphoreCreateMutexStatic(&s_tx_lock_buf);
    s_cmd_queue = xQueueCreate(COMMAND_QUEUE_LEN, sizeof(sl_cmd_t));
    if (s_cmd_queue == NULL) {
        ESP_LOGE(TAG, "cannot create the command queue: out of memory");
        return;
    }
    init_node();

    if (xTaskCreate(network_task, "network", NETWORK_TASK_STACK, NULL, NETWORK_TASK_PRIO, NULL) != pdPASS ||
        xTaskCreate(control_task, "control", CONTROL_TASK_STACK, NULL, CONTROL_TASK_PRIO, NULL) != pdPASS) {
        ESP_LOGE(TAG, "cannot create the tasks: out of memory");
    }
    /* the tasks above keep running */
}

/* A double click on IO0 restarts the board into the other mode (boot_mode.h). The button task starts
 * first and runs in both modes, so even a failed init can be left by the same double click. */
void app_main(void)
{
    const app_mode_t mode = boot_mode_get();
    if (mode_button_start() != ESP_OK) {
        ESP_LOGE(TAG, "mode button (IO0) not available: no mode switch");
    }
    if (mode == APP_MODE_AI_STREAM) {
        stream_mode_run();
    } else {
        run_normal_mode();
    }
    /* app_main returns: the tasks keep running */
}
