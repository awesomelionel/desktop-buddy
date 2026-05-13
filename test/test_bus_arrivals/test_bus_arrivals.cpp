#include <unity.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#include "bus_arrivals.h"

void setUp(void) {}
void tearDown(void) {}

// Pre-computed expected unix timestamps. timegm is non-portable; instead we
// recompute via a deliberately simple unix-epoch helper inside each test
// where we need an authoritative answer.
static int32_t mkTime(int y, int mo, int d, int h, int mi, int s,
                      int off_h, int off_m) {
    static const int dim[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    auto isLeap = [](int yy){
        return (yy % 4 == 0 && yy % 100 != 0) || (yy % 400 == 0);
    };
    int days = 0;
    for (int yr = 1970; yr < y; ++yr) days += isLeap(yr) ? 366 : 365;
    for (int m  = 1;    m  < mo; ++m)  {
        days += dim[m-1];
        if (m == 2 && isLeap(y)) days += 1;
    }
    days += (d - 1);
    int32_t secs = (int32_t)days * 86400 + h * 3600 + mi * 60 + s;
    int32_t off  = off_h * 3600 + (off_h < 0 ? -off_m * 60 : off_m * 60);
    return secs - off;
}

static void test_parses_singapore_offset(void) {
    int32_t got = bus_arrivals::parseIso8601Delta("2026-05-13T17:53:41+08:00");
    int32_t want = mkTime(2026, 5, 13, 17, 53, 41, 8, 0);
    TEST_ASSERT_EQUAL_INT32(want, got);
}

static void test_parses_with_fractional_seconds(void) {
    int32_t got = bus_arrivals::parseIso8601Delta("2026-05-13T17:47:51.46162+08:00");
    int32_t want = mkTime(2026, 5, 13, 17, 47, 51, 8, 0);
    TEST_ASSERT_EQUAL_INT32(want, got);
}

static void test_parses_zero_offset(void) {
    int32_t got = bus_arrivals::parseIso8601Delta("2026-05-13T09:53:41+00:00");
    int32_t want = mkTime(2026, 5, 13, 9, 53, 41, 0, 0);
    TEST_ASSERT_EQUAL_INT32(want, got);
}

static void test_parses_negative_offset(void) {
    int32_t got = bus_arrivals::parseIso8601Delta("2026-05-13T05:53:41-04:00");
    int32_t want = mkTime(2026, 5, 13, 5, 53, 41, -4, 0);
    TEST_ASSERT_EQUAL_INT32(want, got);
}

static void test_rejects_empty(void) {
    TEST_ASSERT_EQUAL_INT32(INT32_MIN, bus_arrivals::parseIso8601Delta(""));
}

static void test_rejects_null(void) {
    TEST_ASSERT_EQUAL_INT32(INT32_MIN, bus_arrivals::parseIso8601Delta(nullptr));
}

static void test_rejects_truncated(void) {
    TEST_ASSERT_EQUAL_INT32(INT32_MIN,
        bus_arrivals::parseIso8601Delta("2026-05-13T17:53"));
}

static void test_rejects_garbage(void) {
    TEST_ASSERT_EQUAL_INT32(INT32_MIN,
        bus_arrivals::parseIso8601Delta("not-a-date"));
}

static void test_rejects_missing_T(void) {
    TEST_ASSERT_EQUAL_INT32(INT32_MIN,
        bus_arrivals::parseIso8601Delta("2026-05-13 17:53:41+08:00"));
}

int main(int /*argc*/, char** /*argv*/) {
    UNITY_BEGIN();
    RUN_TEST(test_parses_singapore_offset);
    RUN_TEST(test_parses_with_fractional_seconds);
    RUN_TEST(test_parses_zero_offset);
    RUN_TEST(test_parses_negative_offset);
    RUN_TEST(test_rejects_empty);
    RUN_TEST(test_rejects_null);
    RUN_TEST(test_rejects_truncated);
    RUN_TEST(test_rejects_garbage);
    RUN_TEST(test_rejects_missing_T);
    return UNITY_END();
}
