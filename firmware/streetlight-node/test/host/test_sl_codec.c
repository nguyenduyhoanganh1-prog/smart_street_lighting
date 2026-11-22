/**
 * @file test_sl_codec.c
 * @brief Host (PC) test of the road-calibration parts of components/sl_net/sl_mqtt_codec.c:
 *        the "recalibrate" command and the streetlight/calib payload.
 *
 * Build (from firmware/streetlight-node/test/host; IDF = your ESP-IDF folder, only its cJSON is used):
 *   gcc -std=c11 -O2 -Wall -Wextra -Werror -I../../../components/sl_net/include -I$IDF/components/json/cJSON \
 *       test_sl_codec.c ../../../components/sl_net/sl_mqtt_codec.c $IDF/components/json/cJSON/cJSON.c -lm \
 *       -o test_sl_codec
 */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "sl_mqtt_codec.h"

static int s_failures;

#define CHECK(cond)                                                                      \
    do {                                                                                 \
        if (!(cond)) {                                                                   \
            printf("FAIL line %d: %s\n", __LINE__, #cond);                             \
            s_failures++;                                                                \
        }                                                                                \
    } while (0)

static bool decode(const char *name, const char *payload, sl_cmd_t *out)
{
    return sl_codec_decode_cmd(name, strlen(name), payload, strlen(payload), out);
}

static void test_recalibrate_command_ignores_its_payload(void)
{
    sl_cmd_t cmd;
    CHECK(decode("recalibrate", "1", &cmd) && cmd.type == SL_CMD_RECALIBRATE);
    CHECK(decode("recalibrate", "{}", &cmd) && cmd.type == SL_CMD_RECALIBRATE);
    /* empty = how a retained message is cleared on the broker: never a command */
    CHECK(!decode("recalibrate", "", &cmd));
    CHECK(!decode("recalibrat", "1", &cmd)); /* a different topic is not the command */
}

static sl_calib_t found_calib(void)
{
    sl_calib_t c;
    memset(&c, 0, sizeof(c));
    c.found = true;
    c.width = 320;
    c.height = 240;
    c.top_a = -0.25f;
    c.top_b = 50.0f;
    c.bottom_a = 0.5f;
    c.bottom_b = 150.0f;
    c.has_left = true;
    c.left_a = 0.1f;
    c.left_b = 20.0f;
    c.has_kerb_top = true;
    c.kerb_top_a = -0.3f;
    c.kerb_top_b = 30.0f;
    c.has_kerb_bottom = false; /* e.g. the lower kerb leaves the frame */
    c.x_min = 20;
    c.x_max = 300;
    c.bounds[0] = 90.0f;
    c.bounds[1] = 160.5f;
    c.bounds[2] = 230.25f;
    c.t_sec = 1795000000;
    return c;
}

static double num_at(const cJSON *arr, int i)
{
    const cJSON *item = cJSON_GetArrayItem(arr, i);
    return cJSON_IsNumber(item) ? item->valuedouble : NAN;
}

static void test_found_calib_round_trips_through_json(void)
{
    const sl_calib_t c = found_calib();
    char buf[512];
    const int n = sl_codec_encode_calib(buf, sizeof(buf), &c);
    CHECK(n > 0 && (size_t)n == strlen(buf));
    cJSON *root = cJSON_Parse(buf);
    CHECK(root != NULL);
    if (root == NULL) {
        return;
    }
    CHECK(strcmp(cJSON_GetObjectItem(root, "state")->valuestring, "found") == 0);
    CHECK(cJSON_GetObjectItem(root, "w")->valueint == 320 && cJSON_GetObjectItem(root, "h")->valueint == 240);
    const cJSON *top = cJSON_GetObjectItem(root, "top");
    CHECK(fabs(num_at(top, 0) + 0.25) < 1e-4 && fabs(num_at(top, 1) - 50.0) < 1e-3);
    CHECK(fabs(num_at(cJSON_GetObjectItem(root, "bottom"), 1) - 150.0) < 1e-3);
    CHECK(fabs(num_at(cJSON_GetObjectItem(root, "left"), 1) - 20.0) < 1e-3);
    CHECK(fabs(num_at(cJSON_GetObjectItem(root, "kerb_top"), 0) + 0.3) < 1e-4);
    CHECK(cJSON_IsNull(cJSON_GetObjectItem(root, "kerb_bottom"))); /* missing side -> null */
    CHECK(cJSON_GetObjectItem(root, "x_min")->valueint == 20 && cJSON_GetObjectItem(root, "x_max")->valueint == 300);
    const cJSON *bounds = cJSON_GetObjectItem(root, "bounds");
    CHECK(cJSON_GetArraySize(bounds) == 3 && fabs(num_at(bounds, 2) - 230.25) < 1e-2);
    CHECK(cJSON_GetObjectItem(root, "t")->valuedouble == 1795000000.0);
    cJSON_Delete(root);
}

static void test_not_found_calib_has_no_geometry(void)
{
    sl_calib_t c = found_calib();
    c.found = false;
    c.t_sec = 0; /* clock not synced yet: no "t" */
    char buf[512];
    CHECK(sl_codec_encode_calib(buf, sizeof(buf), &c) > 0);
    cJSON *root = cJSON_Parse(buf);
    CHECK(root != NULL);
    if (root == NULL) {
        return;
    }
    CHECK(strcmp(cJSON_GetObjectItem(root, "state")->valuestring, "not_found") == 0);
    CHECK(cJSON_GetObjectItem(root, "w")->valueint == 320);
    CHECK(cJSON_GetObjectItem(root, "top") == NULL && cJSON_GetObjectItem(root, "bounds") == NULL);
    CHECK(cJSON_GetObjectItem(root, "t") == NULL);
    cJSON_Delete(root);
}

static void test_calib_bad_arguments(void)
{
    const sl_calib_t c = found_calib();
    char small[32];
    CHECK(sl_codec_encode_calib(small, sizeof(small), &c) == -1); /* does not fit */
    CHECK(sl_codec_encode_calib(small, sizeof(small), NULL) == -1);
}

/* Night base level: set from the dashboard, clamped to 10..100 %, echoed in streetlight/config */
static void test_base_pct_config(void)
{
    sl_cmd_t cmd;
    sl_config_t next;
    char buf[256];

    memset(&cmd, 0, sizeof(cmd));
    CHECK(decode("config", "{\"base_pct\":45}", &cmd));
    CHECK(cmd.type == SL_CMD_CONFIG);
    CHECK(cmd.u.config.fields == SL_CFG_BASE);
    sl_codec_apply_config(&SL_CONFIG_DEFAULT, &cmd.u.config, &next);
    CHECK(next.base_pct == 45);
    CHECK(next.lux_night == SL_CONFIG_DEFAULT.lux_night); /* other fields untouched */
    CHECK(sl_codec_encode_config(buf, sizeof(buf), &next) > 0);
    CHECK(strstr(buf, "\"base_pct\":45") != NULL);

    memset(&cmd, 0, sizeof(cmd));
    CHECK(decode("config", "{\"base_pct\":3}", &cmd));
    CHECK(cmd.u.config.value.base_pct == SL_BASE_MIN_PCT);
    memset(&cmd, 0, sizeof(cmd));
    CHECK(decode("config", "{\"base_pct\":250}", &cmd));
    CHECK(cmd.u.config.value.base_pct == SL_BASE_MAX_PCT);

    CHECK(SL_CONFIG_DEFAULT.base_pct == 20);
}

/* Stopped-object alert switch + time: set from the dashboard, clamped, echoed */
static void test_stop_alert_config(void)
{
    sl_cmd_t cmd;
    sl_config_t next;
    char buf[256];

    memset(&cmd, 0, sizeof(cmd));
    CHECK(decode("config", "{\"stop_alert\":false,\"stop_time_s\":300}", &cmd));
    CHECK(cmd.u.config.fields == (SL_CFG_STOP_ALERT | SL_CFG_STOP_TIME));
    sl_codec_apply_config(&SL_CONFIG_DEFAULT, &cmd.u.config, &next);
    CHECK(next.stop_alert == 0U);
    CHECK(next.stop_time_s == 300U);
    CHECK(sl_codec_encode_config(buf, sizeof(buf), &next) > 0);
    CHECK(strstr(buf, "\"stop_alert\":false") != NULL);
    CHECK(strstr(buf, "\"stop_time_s\":300") != NULL);

    memset(&cmd, 0, sizeof(cmd));
    CHECK(decode("config", "{\"stop_time_s\":1}", &cmd));
    CHECK(cmd.u.config.value.stop_time_s == SL_STOP_TIME_MIN_S);
    memset(&cmd, 0, sizeof(cmd));
    CHECK(!decode("config", "{\"stop_alert\":1}", &cmd)); /* must be a JSON boolean */

    CHECK(SL_CONFIG_DEFAULT.stop_alert == 1U);
}

/* vat_dung alert carries where the object is, for the dashboard to draw on alert/image */
static void test_alert_with_position(void)
{
    char buf[256];
    const sl_alert_t a = {
        .id = 7U, .type = "vat_dung", .zone = 2U, .cls = "xe", .since = 1700000000,
        .status = "open", .has_pos = true, .x = 0.5f, .y = 0.25f, .score = 93U,
    };
    CHECK(sl_codec_encode_alert(buf, sizeof(buf), &a) > 0);
    CHECK(strstr(buf, "\"x\":0.500,\"y\":0.250,\"score\":93") != NULL);
    CHECK(strstr(buf, "\"class\":\"xe\"") != NULL);

    const sl_alert_t b = { .id = 8U, .type = "bh1750" };
    CHECK(sl_codec_encode_alert(buf, sizeof(buf), &b) > 0);
    CHECK(strstr(buf, "\"x\"") == NULL);
    CHECK(strstr(buf, "objs") == NULL);
}

/* Every detection of the picture's frame goes along, so the dashboard dots all of them */
static void test_alert_with_all_objects(void)
{
    char buf[1536];
    sl_alert_t a = { .id = 9U, .type = "vat_dung", .has_pos = true, .x = 0.47f, .y = 0.46f, .score = 100U,
                     .still_s = 12U };
    a.obj_count = 3U;
    a.objs[0] = (sl_alert_obj_t){ .x = 0.47f, .y = 0.46f, .vehicle = true, .score = 100U, .zone = 2U,
                                  .since = 1700000000 };
    a.objs[1] = (sl_alert_obj_t){ .x = 0.48f, .y = 0.62f, .vehicle = false, .score = 71U };
    /* no SNTP time: no "since", but the still time from the tracker still goes */
    a.objs[2] = (sl_alert_obj_t){ .x = 0.9f, .y = 0.1f, .vehicle = true, .score = 88U, .zone = 4U,
                                  .has_still = true, .still_s = 125U };
    CHECK(sl_codec_encode_alert(buf, sizeof(buf), &a) > 0);
    CHECK(strstr(buf, "\"score\":100,\"still_s\":12") != NULL);
    CHECK(strstr(buf, "\"objs\":[{\"x\":0.470,\"y\":0.460,\"class\":\"xe\",\"score\":100,\"zone\":2,"
                      "\"since\":1700000000},{\"x\":0.480,\"y\":0.620,\"class\":\"nguoi\",\"score\":71},"
                      "{\"x\":0.900,\"y\":0.100,\"class\":\"xe\",\"score\":88,\"zone\":4,\"still_s\":125}]") != NULL);

    /* the worst case (10 objects) fits the MQTT TX buffer of sl_mqtt.c (1536 B) */
    a.obj_count = SL_ALERT_MAX_OBJS;
    for (unsigned k = 0; k < SL_ALERT_MAX_OBJS; k++) {
        a.objs[k] = (sl_alert_obj_t){ .x = 0.999f, .y = 0.999f, .vehicle = false, .score = 100U, .zone = 4U, .has_still = true, .still_s = 4294967295U,
                                      .since = 1799999999 };
    }
    a.cls = "nguoi";
    a.status = "open";
    a.since = 1700000000;
    const int n = sl_codec_encode_alert(buf, sizeof(buf), &a);
    CHECK(n > 0 && n < 1536);
}

/* Lamp command with the dashboard send time "t" (latency log); without it sent_unix_ms stays 0 */
static void test_lamp_command_send_time(void)
{
    sl_cmd_t cmd;
    CHECK(decode("lamp", "{\"lamp\":3,\"duty\":100,\"t\":1763771234567}", &cmd));
    CHECK(cmd.type == SL_CMD_LAMP);
    CHECK(cmd.u.lamp.lamp == 3 && cmd.u.lamp.duty == 100);
    CHECK(cmd.sent_unix_ms == 1763771234567LL);
    CHECK(cmd.rx_unix_ms == 0);

    CHECK(decode("lamp", "{\"lamp\":2,\"duty\":20}", &cmd));
    CHECK(cmd.sent_unix_ms == 0);

    CHECK(decode("lamp", "{\"lamp\":2,\"duty\":20,\"t\":12345}", &cmd)); /* not a Unix ms time */
    CHECK(cmd.sent_unix_ms == 0);
    CHECK(decode("lamp", "{\"lamp\":2,\"duty\":20,\"t\":\"now\"}", &cmd));
    CHECK(cmd.sent_unix_ms == 0);
}

/* 6.4 latency test: state carries its send time, the dashboard echoes it back on cmd/latency */
static void test_state_time_and_latency_echo(void)
{
    char buf[512];
    sl_state_t st;
    memset(&st, 0, sizeof(st));
    st.mode = SL_MODE_AUTO;
    CHECK(sl_codec_encode_state(buf, sizeof(buf), &st) > 0);
    CHECK(strstr(buf, "\"t\"") == NULL); /* 0 = omitted */
    st.t_unix_ms = 1763771234567LL;
    CHECK(sl_codec_encode_state(buf, sizeof(buf), &st) > 0);
    CHECK(strstr(buf, "\"t\":1763771234567}") != NULL);

    sl_cmd_t cmd;
    CHECK(decode("latency", "{\"t\":1763771234567,\"rx\":1763771234790}", &cmd));
    CHECK(cmd.type == SL_CMD_LATENCY);
    CHECK(cmd.u.latency.board_ms == 1763771234567LL);
    CHECK(cmd.u.latency.web_ms == 1763771234790LL);
    CHECK(!decode("latency", "{\"t\":1763771234567}", &cmd));         /* rx missing */
    CHECK(!decode("latency", "{\"t\":5,\"rx\":1763771234790}", &cmd)); /* not a time */
}

int main(void)
{
    test_state_time_and_latency_echo();
    test_lamp_command_send_time();
    test_base_pct_config();
    test_stop_alert_config();
    test_alert_with_position();
    test_alert_with_all_objects();
    test_recalibrate_command_ignores_its_payload();
    test_found_calib_round_trips_through_json();
    test_not_found_calib_has_no_geometry();
    test_calib_bad_arguments();
    printf(s_failures == 0 ? "all sl_codec tests passed\n" : "%d check(s) failed\n", s_failures);
    return s_failures == 0 ? 0 : 1;
}
