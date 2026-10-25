/**
 * @file sl_mqtt.c
 * @brief MQTT/TLS client for the streetlight node (see sl_mqtt.h).
 *
 * Threading: esp-mqtt runs its own task and calls on_mqtt_event(). That
 * handler only decodes commands and pushes them to a static queue; the app's
 * control task consumes them with sl_mqtt_wait_event(). Publishers share one
 * static TX buffer guarded by a mutex (no heap in the publish path).
 */
#include "sl_mqtt.h"

#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "mbedtls/base64.h"
#include "mqtt_client.h"

#define TAG "sl_mqtt"

#define CMD_PREFIX         SL_MQTT_TOPIC_PREFIX "/cmd/"
#define TOPIC_ONLINE       SL_MQTT_TOPIC_PREFIX "/online"
#define TOPIC_MAX_LEN      48U
#define TX_BUF_SIZE        1536U /* fits energy/history (~1.1 KB worst case) */
#define EVENT_QUEUE_LEN    8U
#define KEEPALIVE_S        30
#define QOS_DEFAULT        1
#define LOCK_TIMEOUT_MS    200U
#define IMAGE_LOCK_TIMEOUT_MS 2000U
/* base64 output size for the largest accepted JPEG, plus NUL */
#define IMAGE_B64_CAP      (((SL_MQTT_IMAGE_MAX_JPEG + 2U) / 3U) * 4U + 1U)

static esp_mqtt_client_handle_t s_client;
static atomic_bool s_connected;

static StaticQueue_t s_queue_ctrl;
static uint8_t s_queue_storage[EVENT_QUEUE_LEN * sizeof(sl_mqtt_event_t)];
static QueueHandle_t s_queue;

static StaticSemaphore_t s_tx_lock_ctrl;
static SemaphoreHandle_t s_tx_lock;
static char s_tx_buf[TX_BUF_SIZE];

static StaticSemaphore_t s_img_lock_ctrl;
static SemaphoreHandle_t s_img_lock;
static char *s_img_buf; /* PSRAM, allocated once in sl_mqtt_start() */

/* ============================== Internal helpers ============================== */

static esp_err_t publish_raw(const char *sub_topic, const char *payload, size_t len, bool retain)
{
    if (s_client == NULL || !atomic_load(&s_connected)) {
        return ESP_ERR_INVALID_STATE;
    }
    char topic[TOPIC_MAX_LEN];
    const int n = snprintf(topic, sizeof(topic), SL_MQTT_TOPIC_PREFIX "/%s", sub_topic);
    if (n < 0 || (size_t)n >= sizeof(topic)) {
        return ESP_ERR_INVALID_ARG;
    }
    /* esp-mqtt uses strlen(payload) when len == 0; an empty retained payload
     * deletes the retained message on the broker */
    const int msg_id = esp_mqtt_client_publish(s_client, topic, payload, (int)len,
                                               QOS_DEFAULT, retain ? 1 : 0);
    if (msg_id < 0) {
        ESP_LOGW(TAG, "publish %s failed", topic);
        return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t lock_take(SemaphoreHandle_t lock, uint32_t timeout_ms)
{
    if (lock == NULL) {
        return ESP_ERR_INVALID_STATE; /* sl_mqtt_start() not called */
    }
    return xSemaphoreTake(lock, pdMS_TO_TICKS(timeout_ms)) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}

/* Publish s_tx_buf after an encoder wrote `len` bytes (caller holds s_tx_lock) */
static esp_err_t publish_tx(const char *sub_topic, int len)
{
    if (len < 0) {
        ESP_LOGE(TAG, "%s: payload does not fit TX buffer or invalid input", sub_topic);
        return ESP_ERR_INVALID_SIZE;
    }
    return publish_raw(sub_topic, s_tx_buf, (size_t)len, true);
}

static void post_event(const sl_mqtt_event_t *ev)
{
    if (xQueueSend(s_queue, ev, 0) != pdTRUE) {
        ESP_LOGW(TAG, "event queue full, event %d dropped", (int)ev->type);
    }
}

static void handle_data(const esp_mqtt_event_handle_t ev)
{
    /* Commands are tiny; a fragmented message is not a valid command */
    if (ev->current_data_offset != 0 || ev->data_len != ev->total_data_len) {
        ESP_LOGW(TAG, "fragmented message ignored (%d bytes)", ev->total_data_len);
        return;
    }
    const size_t prefix_len = strlen(CMD_PREFIX);
    if (ev->topic == NULL || (size_t)ev->topic_len <= prefix_len
        || memcmp(ev->topic, CMD_PREFIX, prefix_len) != 0) {
        return;
    }
    sl_mqtt_event_t out = { .type = SL_MQTT_EVT_COMMAND };
    const char *name = ev->topic + prefix_len;
    const size_t name_len = (size_t)ev->topic_len - prefix_len;
    if (!sl_codec_decode_cmd(name, name_len, ev->data, (size_t)ev->data_len, &out.cmd)) {
        ESP_LOGW(TAG, "invalid command on %.*s: %.*s", ev->topic_len, ev->topic,
                 ev->data_len, ev->data);
        return;
    }
    post_event(&out);
}

static void log_error(const esp_mqtt_error_codes_t *err)
{
    if (err == NULL) {
        return;
    }
    if (err->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED) {
        /* 4/5 = bad username/password or not authorized */
        ESP_LOGE(TAG, "broker refused connection, code %d (check HiveMQ credentials)",
                 (int)err->connect_return_code);
    } else if (err->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
        ESP_LOGE(TAG, "transport error: tls=0x%x stack=0x%x errno=%d",
                 err->esp_tls_last_esp_err, err->esp_tls_stack_err,
                 err->esp_transport_sock_errno);
    }
}

static void on_mqtt_event(void *arg, esp_event_base_t base, int32_t event_id, void *event_data)
{
    (void)arg;
    (void)base;
    const esp_mqtt_event_handle_t ev = event_data;
    sl_mqtt_event_t out = { 0 };

    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "connected to broker");
        atomic_store(&s_connected, true);
        (void)publish_raw("online", "online", 0, true); /* overrides the LWT "offline" */
        if (esp_mqtt_client_subscribe(s_client, CMD_PREFIX "#", QOS_DEFAULT) < 0) {
            ESP_LOGE(TAG, "subscribe " CMD_PREFIX "# failed");
        }
        out.type = SL_MQTT_EVT_CONNECTED;
        post_event(&out);
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "disconnected, esp-mqtt will reconnect");
        atomic_store(&s_connected, false);
        out.type = SL_MQTT_EVT_DISCONNECTED;
        post_event(&out);
        break;
    case MQTT_EVENT_DATA:
        handle_data(ev);
        break;
    case MQTT_EVENT_ERROR:
        log_error(ev->error_handle);
        break;
    default:
        break;
    }
}

/* ================================= Public API ================================= */

esp_err_t sl_mqtt_start(const sl_mqtt_config_t *cfg)
{
    if (cfg == NULL || cfg->uri == NULL || cfg->username == NULL || cfg->password == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_client != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    s_queue = xQueueCreateStatic(EVENT_QUEUE_LEN, sizeof(sl_mqtt_event_t),
                                 s_queue_storage, &s_queue_ctrl);
    s_tx_lock = xSemaphoreCreateMutexStatic(&s_tx_lock_ctrl);
    s_img_lock = xSemaphoreCreateMutexStatic(&s_img_lock_ctrl);

    /* One-time allocation at startup; images are simply disabled without PSRAM */
    s_img_buf = heap_caps_malloc(IMAGE_B64_CAP, MALLOC_CAP_SPIRAM);
    if (s_img_buf == NULL) {
        ESP_LOGW(TAG, "no PSRAM for image buffer, alert/image disabled");
    }

    const esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = cfg->uri,
        .broker.verification.crt_bundle_attach = esp_crt_bundle_attach, /* TLS, verifies HiveMQ cert */
        .credentials.username = cfg->username,
        .credentials.client_id = cfg->client_id,
        .credentials.authentication.password = cfg->password,
        .session.keepalive = KEEPALIVE_S,
        .session.last_will = {
            .topic = TOPIC_ONLINE,
            .msg = "offline",
            .qos = QOS_DEFAULT,
            .retain = 1,
        },
    };
    s_client = esp_mqtt_client_init(&mqtt_cfg);
    if (s_client == NULL) {
        return ESP_FAIL;
    }
    esp_err_t err = esp_mqtt_client_register_event(s_client, ESP_EVENT_ANY_ID, on_mqtt_event, NULL);
    if (err == ESP_OK) {
        err = esp_mqtt_client_start(s_client);
    }
    if (err != ESP_OK) {
        esp_mqtt_client_destroy(s_client);
        s_client = NULL;
    }
    return err;
}

bool sl_mqtt_is_connected(void)
{
    return atomic_load(&s_connected);
}

bool sl_mqtt_wait_event(sl_mqtt_event_t *out, uint32_t timeout_ms)
{
    if (out == NULL || s_queue == NULL) {
        return false;
    }
    const TickType_t ticks = (timeout_ms == SL_MQTT_WAIT_FOREVER) ? portMAX_DELAY
                                                                  : pdMS_TO_TICKS(timeout_ms);
    return xQueueReceive(s_queue, out, ticks) == pdTRUE;
}

esp_err_t sl_mqtt_publish_state(const sl_state_t *state)
{
    esp_err_t err = lock_take(s_tx_lock, LOCK_TIMEOUT_MS);
    if (err == ESP_OK) {
        err = publish_tx("state", sl_codec_encode_state(s_tx_buf, sizeof(s_tx_buf), state));
        xSemaphoreGive(s_tx_lock);
    }
    return err;
}

esp_err_t sl_mqtt_publish_energy(const sl_lamp_energy_t lamps[SL_LAMP_COUNT])
{
    esp_err_t err = lock_take(s_tx_lock, LOCK_TIMEOUT_MS);
    if (err == ESP_OK) {
        err = publish_tx("energy", sl_codec_encode_energy(s_tx_buf, sizeof(s_tx_buf), lamps));
        xSemaphoreGive(s_tx_lock);
    }
    return err;
}

esp_err_t sl_mqtt_publish_history(int64_t t_sec,
                                  const uint32_t mwh[SL_LAMP_COUNT][SL_HISTORY_HOURS])
{
    esp_err_t err = lock_take(s_tx_lock, LOCK_TIMEOUT_MS);
    if (err == ESP_OK) {
        err = publish_tx("energy/history",
                         sl_codec_encode_history(s_tx_buf, sizeof(s_tx_buf), t_sec, mwh));
        xSemaphoreGive(s_tx_lock);
    }
    return err;
}

esp_err_t sl_mqtt_publish_config(const sl_config_t *cfg)
{
    esp_err_t err = lock_take(s_tx_lock, LOCK_TIMEOUT_MS);
    if (err == ESP_OK) {
        err = publish_tx("config", sl_codec_encode_config(s_tx_buf, sizeof(s_tx_buf), cfg));
        xSemaphoreGive(s_tx_lock);
    }
    return err;
}

esp_err_t sl_mqtt_publish_alert(const sl_alert_t *alert)
{
    esp_err_t err = lock_take(s_tx_lock, LOCK_TIMEOUT_MS);
    if (err == ESP_OK) {
        err = publish_tx("alert", sl_codec_encode_alert(s_tx_buf, sizeof(s_tx_buf), alert));
        xSemaphoreGive(s_tx_lock);
    }
    return err;
}

esp_err_t sl_mqtt_clear_alert(void)
{
    const esp_err_t err = publish_raw("alert", "", 0, true);
    if (err != ESP_OK) {
        return err;
    }
    return publish_raw("alert/image", "", 0, true);
}

esp_err_t sl_mqtt_publish_image(const uint8_t *jpeg, size_t len)
{
    if (jpeg == NULL || len == 0U) {
        return ESP_ERR_INVALID_ARG;
    }
    if (len > SL_MQTT_IMAGE_MAX_JPEG) {
        ESP_LOGW(TAG, "JPEG %u B > limit %u B, lower camera resolution/quality",
                 (unsigned)len, (unsigned)SL_MQTT_IMAGE_MAX_JPEG);
        return ESP_ERR_INVALID_SIZE;
    }
    if (s_img_buf == NULL) {
        return ESP_ERR_NO_MEM;
    }
    esp_err_t err = lock_take(s_img_lock, IMAGE_LOCK_TIMEOUT_MS);
    if (err != ESP_OK) {
        return err;
    }
    size_t b64_len = 0;
    const int rc = mbedtls_base64_encode((unsigned char *)s_img_buf, IMAGE_B64_CAP, &b64_len,
                                         jpeg, len);
    err = (rc == 0) ? publish_raw("alert/image", s_img_buf, b64_len, true) : ESP_FAIL;
    xSemaphoreGive(s_img_lock);
    return err;
}
