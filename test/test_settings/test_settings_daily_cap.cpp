#include <unity.h>
#include <string.h>
#include "settings_model.h"

using namespace settings;

static Settings make_defaults_cap() {
    Settings s = {};
    setDefaults(s, "Claude-AABB");
    return s;
}

void test_daily_cap_default_zero(void) {
    Settings s = make_defaults_cap();
    TEST_ASSERT_EQUAL_UINT32(0u, s.daily_token_cap);
}

void test_daily_cap_zero_accepted(void) {
    Settings s = make_defaults_cap();
    s.daily_token_cap = 0;
    char err[64] = {};
    TEST_ASSERT_TRUE(validate(s, err, sizeof(err)));
}

void test_daily_cap_in_range_accepted(void) {
    Settings s = make_defaults_cap();
    s.daily_token_cap = 200000u;
    char err[64] = {};
    TEST_ASSERT_TRUE(validate(s, err, sizeof(err)));
}

void test_daily_cap_at_max_accepted(void) {
    Settings s = make_defaults_cap();
    s.daily_token_cap = DAILY_TOKEN_CAP_MAX;
    char err[64] = {};
    TEST_ASSERT_TRUE(validate(s, err, sizeof(err)));
}

void test_daily_cap_over_max_rejected(void) {
    Settings s = make_defaults_cap();
    s.daily_token_cap = DAILY_TOKEN_CAP_MAX + 1u;
    char err[64] = {};
    TEST_ASSERT_FALSE(validate(s, err, sizeof(err)));
    TEST_ASSERT_TRUE(strstr(err, "daily_token_cap") != nullptr);
}

void test_apply_daily_cap_ok(void) {
    Settings s = make_defaults_cap();
    char err[64] = {};
    TEST_ASSERT_TRUE(applyDailyCapField(s, 250000u, err, sizeof(err)));
    TEST_ASSERT_EQUAL_UINT32(250000u, s.daily_token_cap);
}

void test_apply_daily_cap_rejects_over_max(void) {
    Settings s = make_defaults_cap();
    s.daily_token_cap = 1234u;
    char err[64] = {};
    TEST_ASSERT_FALSE(applyDailyCapField(s, DAILY_TOKEN_CAP_MAX + 1u,
                                          err, sizeof(err)));
    // Unchanged on failure.
    TEST_ASSERT_EQUAL_UINT32(1234u, s.daily_token_cap);
}

void test_tojson_includes_daily_cap(void) {
    Settings s = make_defaults_cap();
    s.daily_token_cap = 12345u;
    char buf[1024] = {};
    size_t n = toJson(s, buf, sizeof(buf));
    TEST_ASSERT_NOT_EQUAL(0, n);
    TEST_ASSERT_TRUE(strstr(buf, "\"daily_token_cap\":12345") != nullptr);
}
