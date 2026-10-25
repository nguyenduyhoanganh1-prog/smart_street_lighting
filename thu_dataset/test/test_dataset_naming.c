/**
 * @file test_dataset_naming.c
 * @brief Off-target test of the pure naming rules. Build and run on a PC:
 *
 *   gcc -Wall -Wextra -Werror -I../main test_dataset_naming.c ../main/dataset_naming.c -o test_naming
 *   ./test_naming
 */
#include <stdio.h>
#include <string.h>

#include "dataset_naming.h"

static int s_failures;

#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            printf("FAIL line %d: %s\n", __LINE__, #cond);                       \
            s_failures++;                                                        \
        }                                                                        \
    } while (0)

static void test_labels(void)
{
    CHECK(dataset_label_valid("nguoi"));
    CHECK(dataset_label_valid("xe"));
    CHECK(dataset_label_valid("nen"));
    CHECK(!dataset_label_valid("Nguoi"));
    CHECK(!dataset_label_valid(""));
    CHECK(!dataset_label_valid("nguoi.jpg"));
    CHECK(!dataset_label_valid(NULL));
}

static void test_period(void)
{
    CHECK(!dataset_period_valid(0U));
    CHECK(dataset_period_valid(1U));
    CHECK(dataset_period_valid(5U));
    CHECK(dataset_period_valid(3600U));
    CHECK(!dataset_period_valid(3601U));
}

static void test_frame_name(void)
{
    char buf[DATASET_FRAME_NAME_CAP];
    CHECK(dataset_frame_name(buf, sizeof(buf), "nguoi", 7U) == 14);
    CHECK(strcmp(buf, "nguoi.0007.pgm") == 0);
    CHECK(dataset_frame_name(buf, sizeof(buf), "xe", 12345U) > 0);
    CHECK(strcmp(buf, "xe.12345.pgm") == 0);
    CHECK(dataset_frame_name(buf, sizeof(buf), "bad", 1U) == -1);
    CHECK(dataset_frame_name(buf, sizeof(buf), "nen", 0U) == -1);
    CHECK(dataset_frame_name(buf, sizeof(buf), "nen", DATASET_SEQ_MAX + 1U) == -1);
    CHECK(dataset_frame_name(buf, 8U, "nguoi", 1U) == -1); /* buffer too small */
    CHECK(dataset_frame_name(NULL, 8U, "nguoi", 1U) == -1);
}

static void test_session_names(void)
{
    char buf[DATASET_SESSION_NAME_CAP];
    CHECK(dataset_session_dir_name(buf, sizeof(buf), 1U) == 9);
    CHECK(strcmp(buf, "phien_001") == 0);
    CHECK(dataset_session_dir_name(buf, sizeof(buf), 1234U) > 0);
    CHECK(strcmp(buf, "phien_1234") == 0);
    CHECK(dataset_session_dir_name(buf, sizeof(buf), 0U) == -1);
    CHECK(dataset_session_dir_name(buf, sizeof(buf), DATASET_SESSION_MAX + 1U) == -1);
}

static void test_parse_session(void)
{
    uint32_t n = 0;
    CHECK(dataset_parse_session_dir("phien_001", &n) && n == 1U);
    CHECK(dataset_parse_session_dir("phien_42", &n) && n == 42U);
    CHECK(dataset_parse_session_dir("phien_9999", &n) && n == 9999U);
    CHECK(!dataset_parse_session_dir("phien_0", &n));
    CHECK(!dataset_parse_session_dir("phien_", &n));
    CHECK(!dataset_parse_session_dir("phien_12a", &n));
    CHECK(!dataset_parse_session_dir("phien_12345", &n)); /* more than 4 digits */
    CHECK(!dataset_parse_session_dir("session_001", &n));
    CHECK(!dataset_parse_session_dir("phien_001", NULL));
    CHECK(!dataset_parse_session_dir(NULL, &n));
}

int main(void)
{
    test_labels();
    test_period();
    test_frame_name();
    test_session_names();
    test_parse_session();
    if (s_failures == 0) {
        printf("all dataset_naming tests passed\n");
        return 0;
    }
    printf("%d check(s) failed\n", s_failures);
    return 1;
}
