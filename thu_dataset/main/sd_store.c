/**
 * @file sd_store.c
 * @brief SD card storage of the dataset (see sd_store.h).
 */
#include "sd_store.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "dataset_naming.h"
#include "driver/sdmmc_host.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "gray_image.h"
#include "sdmmc_cmd.h"

#define TAG "sd_store"

#define PATH_CAP         96U
#define DIR_MODE         0775
#define MAX_OPEN_FILES   3
#define ALLOC_UNIT_BYTES (16U * 1024U)
#define SD_FREQ_KHZ      10000   /* 10 MHz: 20 MHz gave CRC errors with the weak internal pull-ups */
#define REMOUNT_DELAY_MS 200U    /* let the card finish an interrupted write before it is reset */
#define INDEX_HEADER     "file,label,lamp1,lamp2,lamp3,lamp4,exposure_locked,epoch\n"

static sdmmc_card_t *s_card;
static bool s_mounted;
static char s_path[PATH_CAP]; /* scratch path, see the thread-safety note in sd_store.h */

/* ================================== Helpers ================================== */

static bool build_path(uint32_t session_no, const char *file)
{
    char dir[DATASET_SESSION_NAME_CAP];
    if (dataset_session_dir_name(dir, sizeof(dir), session_no) < 0) {
        return false;
    }
    const int n = snprintf(s_path, sizeof(s_path), APP_SD_DATASET_DIR "/%s/%s", dir, file);
    return n > 0 && (size_t)n < sizeof(s_path);
}

static esp_err_t ensure_dir(const char *path)
{
    if (mkdir(path, DIR_MODE) == 0 || errno == EEXIST) {
        return ESP_OK;
    }
    ESP_LOGE(TAG, "mkdir %s failed: errno %d", path, errno);
    return ESP_FAIL;
}

/* A listing error must not read as "no sessions": session 1 would then be overwritten */
static esp_err_t highest_session(uint32_t *best_out)
{
    DIR *dir = opendir(APP_SD_DATASET_DIR);
    if (dir == NULL) {
        ESP_LOGE(TAG, "cannot list %s (errno %d)", APP_SD_DATASET_DIR, errno);
        return ESP_FAIL;
    }
    uint32_t best = 0;
    const struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        uint32_t number = 0;
        if (dataset_parse_session_dir(entry->d_name, &number) && number > best) {
            best = number;
        }
    }
    closedir(dir);
    *best_out = best;
    return ESP_OK;
}

/*
 * After a bus error (e.g. CRC) the card stays busy and every later command fails until it is
 * re-initialised. Unmount + mount sends CMD0, which resets the card without a board reset.
 */
static esp_err_t remount(void)
{
    ESP_LOGW(TAG, "SD access failed, re-initialising the card");
    if (s_card != NULL) {
        (void)esp_vfs_fat_sdcard_unmount(APP_SD_MOUNT_POINT, s_card);
        s_card = NULL;
    }
    s_mounted = false;
    vTaskDelay(pdMS_TO_TICKS(REMOUNT_DELAY_MS));
    return sd_store_mount();
}

/* fclose() flushes the data, so its result matters as much as the writes */
static esp_err_t finish_file(FILE *f, bool writes_ok)
{
    const int close_rc = fclose(f);
    return (writes_ok && close_rc == 0) ? ESP_OK : ESP_FAIL;
}

static esp_err_t write_session_json(uint32_t session_no, int64_t epoch_s, bool exposure_locked)
{
    if (!build_path(session_no, "session.json")) {
        return ESP_ERR_INVALID_ARG;
    }
    FILE *f = fopen(s_path, "w");
    if (f == NULL) {
        ESP_LOGE(TAG, "cannot create %s (errno %d)", s_path, errno);
        return ESP_FAIL;
    }
    bool ok = fprintf(f,
                      "{\"session\":%u,\"epoch\":%lld,\"width\":%u,\"height\":%u,"
                      "\"format\":\"PGM P5 8-bit grayscale\",\"source\":\"QVGA 320x240 gray, full frame\","
                      "\"exposure_locked\":%s,\"labels\":[",
                      (unsigned)session_no, (long long)epoch_s, (unsigned)APP_FRAME_WIDTH,
                      (unsigned)APP_FRAME_HEIGHT, exposure_locked ? "true" : "false")
              > 0;
    for (size_t i = 0; ok && i < DATASET_LABEL_COUNT; i++) {
        ok = fprintf(f, "%s\"%s\"", i > 0U ? "," : "", DATASET_LABELS[i]) > 0;
    }
    ok = ok && fprintf(f, "]}\n") > 0;
    return finish_file(f, ok);
}

static esp_err_t write_index_header(uint32_t session_no)
{
    if (!build_path(session_no, "index.csv")) {
        return ESP_ERR_INVALID_ARG;
    }
    FILE *f = fopen(s_path, "w");
    if (f == NULL) {
        ESP_LOGE(TAG, "cannot create %s (errno %d)", s_path, errno);
        return ESP_FAIL;
    }
    return finish_file(f, fputs(INDEX_HEADER, f) >= 0);
}

static esp_err_t append_index_line(uint32_t session_no, const char *name, const char *label,
                                   const sd_frame_meta_t *meta)
{
    if (!build_path(session_no, "index.csv")) {
        return ESP_ERR_INVALID_ARG;
    }
    FILE *f = fopen(s_path, "a");
    if (f == NULL) {
        return ESP_FAIL;
    }
    const bool ok = fprintf(f, "%s,%s,%u,%u,%u,%u,%u,%lld\n", name, label, (unsigned)meta->lamp_pct[0],
                            (unsigned)meta->lamp_pct[1], (unsigned)meta->lamp_pct[2],
                            (unsigned)meta->lamp_pct[3], meta->exposure_locked ? 1U : 0U,
                            (long long)meta->epoch_s)
                    > 0;
    return finish_file(f, ok);
}

static esp_err_t create_session(uint32_t session_no, int64_t epoch_s, bool exposure_locked)
{
    char dir[DATASET_SESSION_NAME_CAP];
    if (dataset_session_dir_name(dir, sizeof(dir), session_no) < 0) {
        ESP_LOGE(TAG, "too many sessions on the card (max %u)", (unsigned)DATASET_SESSION_MAX);
        return ESP_ERR_NO_MEM;
    }
    const int n = snprintf(s_path, sizeof(s_path), APP_SD_DATASET_DIR "/%s", dir);
    if (n <= 0 || (size_t)n >= sizeof(s_path)) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = ensure_dir(s_path);
    if (err == ESP_OK) {
        err = write_session_json(session_no, epoch_s, exposure_locked);
    }
    if (err == ESP_OK) {
        err = write_index_header(session_no);
    }
    return err;
}

/* Writes the frame to s_path (already built by the caller) */
static esp_err_t write_frame_file(const char *header, size_t header_len, const uint8_t *data, size_t len)
{
    FILE *f = fopen(s_path, "wb");
    if (f == NULL) {
        ESP_LOGE(TAG, "cannot create %s (errno %d)", s_path, errno);
        return ESP_FAIL;
    }
    const bool wrote_all = fwrite(header, 1, header_len, f) == header_len
                           && fwrite(data, 1, len, f) == len;
    if (finish_file(f, wrote_all) != ESP_OK) {
        ESP_LOGE(TAG, "write of %s failed", s_path);
        (void)remove(s_path); /* never leave a truncated frame in the dataset */
        return ESP_FAIL;
    }
    return ESP_OK;
}

/* ================================= Public API ================================= */

esp_err_t sd_store_mount(void)
{
    const esp_vfs_fat_sdmmc_mount_config_t mount_cfg = {
        .format_if_mount_failed = false, /* never erase a card automatically */
        .max_files = MAX_OPEN_FILES,
        .allocation_unit_size = ALLOC_UNIT_BYTES,
    };
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.max_freq_khz = SD_FREQ_KHZ;

    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.width = 1;
    slot.clk = APP_SD_CLK_GPIO;
    slot.cmd = APP_SD_CMD_GPIO;
    slot.d0 = APP_SD_D0_GPIO;
    slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    const esp_err_t err = esp_vfs_fat_sdmmc_mount(APP_SD_MOUNT_POINT, &host, &slot, &mount_cfg, &s_card);
    if (err != ESP_OK) {
        if (err == ESP_FAIL) {
            ESP_LOGE(TAG, "card found but the FAT file system did not mount: format the card as FAT32");
        } else {
            ESP_LOGE(TAG, "SD init failed: %s (no card? check APP_SD_*_GPIO against the schematic)",
                     esp_err_to_name(err));
        }
        return err;
    }
    s_mounted = true;
    sdmmc_card_print_info(stdout, s_card);
    if (ensure_dir(APP_SD_DATASET_DIR) != ESP_OK) {
        s_mounted = false;
        return ESP_FAIL;
    }
    return ESP_OK;
}

bool sd_store_is_mounted(void)
{
    return s_mounted;
}

esp_err_t sd_store_space(uint64_t *total_bytes, uint64_t *free_bytes)
{
    if (!s_mounted || total_bytes == NULL || free_bytes == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    return esp_vfs_fat_info(APP_SD_MOUNT_POINT, total_bytes, free_bytes);
}

esp_err_t sd_store_begin_session(uint32_t *session_no, int64_t epoch_s, bool exposure_locked)
{
    if (session_no == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_mounted && remount() != ESP_OK) { /* an earlier re-init failed: try again */
        return ESP_ERR_INVALID_STATE;
    }
    uint32_t last = 0;
    esp_err_t err = highest_session(&last);
    if (err != ESP_OK && remount() == ESP_OK) {
        err = highest_session(&last);
    }
    if (err != ESP_OK) {
        return err;
    }
    const uint32_t next = last + 1U;
    err = create_session(next, epoch_s, exposure_locked);
    if (err == ESP_FAIL && remount() == ESP_OK) {
        /* same number: the retry completes the folder the failed attempt may have created */
        err = create_session(next, epoch_s, exposure_locked);
    }
    if (err != ESP_OK) {
        return err;
    }
    *session_no = next;
    ESP_LOGI(TAG, "session %u started", (unsigned)next);
    return ESP_OK;
}

esp_err_t sd_store_save_frame(uint32_t session_no, const char *label, uint32_t seq,
                              const uint8_t *data, size_t len, const sd_frame_meta_t *meta,
                              char *name_out, size_t name_cap)
{
    if (!s_mounted && remount() != ESP_OK) { /* an earlier re-init failed: try again */
        return ESP_ERR_INVALID_STATE;
    }
    char header[GRAY_PGM_HEADER_CAP];
    const int header_len = gray_pgm_header(header, sizeof(header), APP_FRAME_WIDTH, APP_FRAME_HEIGHT);
    if (data == NULL || meta == NULL || name_out == NULL || len != APP_FRAME_BYTES || header_len < 0
        || dataset_frame_name(name_out, name_cap, label, seq) < 0 || !build_path(session_no, name_out)) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = write_frame_file(header, (size_t)header_len, data, len);
    if (err != ESP_OK && remount() == ESP_OK) {
        err = write_frame_file(header, (size_t)header_len, data, len); /* s_path is kept by remount() */
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "frame not saved even after re-initialising the card (card full or faulty?)");
        return err;
    }
    if (append_index_line(session_no, name_out, label, meta) != ESP_OK) {
        ESP_LOGW(TAG, "frame saved but index.csv could not be updated");
    }
    return ESP_OK;
}
