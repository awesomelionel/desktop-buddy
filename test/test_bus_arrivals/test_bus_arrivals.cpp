#include <unity.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
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

static const char* kSampleJson = R"({
  "busStops": [{
    "BusStopCode": 50171,
    "Services": [
      {"ServiceNo": "21",  "NextBus":  {"EstimatedArrival": "2026-05-13T17:53:41+08:00", "Load": "SEA", "Feature": "WAB", "Type": "DD"}},
      {"ServiceNo": "129", "NextBus":  {"EstimatedArrival": "2026-05-13T17:50:48+08:00", "Load": "SEA", "Feature": "WAB", "Type": "DD"}},
      {"ServiceNo": "961M","NextBus":  {"EstimatedArrival": "2026-05-13T17:51:14+08:00", "Load": "SDA", "Feature": "",    "Type": "SD"}}
    ],
    "UpdatedAt": "2026-05-13T17:47:51.46162+08:00"
  }]
})";

static void test_parses_three_services(void) {
    bus_arrivals::BusStopArrivals out{};
    TEST_ASSERT_TRUE(bus_arrivals::parseBusArrivalsJson(
        kSampleJson, strlen(kSampleJson), out));
    TEST_ASSERT_EQUAL_UINT8(3, out.service_count);
    TEST_ASSERT_EQUAL_STRING("21",  out.services[0].service_no);
    TEST_ASSERT_EQUAL_STRING("129", out.services[1].service_no);
    TEST_ASSERT_EQUAL_STRING("961M", out.services[2].service_no);
    TEST_ASSERT_EQUAL_INT(bus_arrivals::LOAD_SEA, out.services[0].load);
    TEST_ASSERT_EQUAL_INT(bus_arrivals::LOAD_SDA, out.services[2].load);
    TEST_ASSERT_EQUAL_INT(bus_arrivals::TYPE_DD,  out.services[0].type);
    TEST_ASSERT_EQUAL_INT(bus_arrivals::TYPE_SD,  out.services[2].type);
    TEST_ASSERT_TRUE(out.valid);
    TEST_ASSERT_EQUAL_STRING("", out.last_error);
}

static void test_eta_relative_to_updatedat(void) {
    bus_arrivals::BusStopArrivals out{};
    TEST_ASSERT_TRUE(bus_arrivals::parseBusArrivalsJson(
        kSampleJson, strlen(kSampleJson), out));
    // 17:53:41 - 17:47:51 = 5m 50s = 350s. We accept 349..351 to absorb
    // the dropped fractional-seconds part of UpdatedAt (.46162 -> 0).
    TEST_ASSERT_INT_WITHIN(2, 350, out.services[0].eta_seconds_at_fetch);
}

static void test_truncates_at_max_services(void) {
    // Build a JSON with 25 services; expect first 16 to land.
    char buf[8192];
    int pos = snprintf(buf, sizeof(buf),
        "{\"busStops\":[{\"BusStopCode\":1,\"UpdatedAt\":"
        "\"2026-05-13T17:47:51+08:00\",\"Services\":[");
    for (int i = 0; i < 25; ++i) {
        pos += snprintf(buf + pos, sizeof(buf) - pos,
            "%s{\"ServiceNo\":\"%d\",\"NextBus\":{"
            "\"EstimatedArrival\":\"2026-05-13T17:50:00+08:00\","
            "\"Load\":\"SEA\",\"Type\":\"SD\"}}",
            i == 0 ? "" : ",", i);
    }
    pos += snprintf(buf + pos, sizeof(buf) - pos, "]}]}");
    bus_arrivals::BusStopArrivals out{};
    TEST_ASSERT_TRUE(bus_arrivals::parseBusArrivalsJson(buf, (size_t)pos, out));
    TEST_ASSERT_EQUAL_UINT8(bus_arrivals::kMaxServicesPerStop,
                            out.service_count);
}

static void test_unknown_load_and_type_become_unknown(void) {
    const char* j = R"({"busStops":[{"BusStopCode":1,
      "UpdatedAt":"2026-05-13T17:47:51+08:00",
      "Services":[{"ServiceNo":"99","NextBus":{
        "EstimatedArrival":"2026-05-13T17:50:00+08:00",
        "Load":"???","Type":"??"}}]}]})";
    bus_arrivals::BusStopArrivals out{};
    TEST_ASSERT_TRUE(bus_arrivals::parseBusArrivalsJson(j, strlen(j), out));
    TEST_ASSERT_EQUAL_INT(bus_arrivals::LOAD_UNKNOWN, out.services[0].load);
    TEST_ASSERT_EQUAL_INT(bus_arrivals::TYPE_UNKNOWN, out.services[0].type);
}

static void test_missing_load_and_type_become_unknown(void) {
    const char* j = R"({"busStops":[{"BusStopCode":1,
      "UpdatedAt":"2026-05-13T17:47:51+08:00",
      "Services":[{"ServiceNo":"7","NextBus":{
        "EstimatedArrival":"2026-05-13T17:50:00+08:00"}}]}]})";
    bus_arrivals::BusStopArrivals out{};
    TEST_ASSERT_TRUE(bus_arrivals::parseBusArrivalsJson(j, strlen(j), out));
    TEST_ASSERT_EQUAL_INT(bus_arrivals::LOAD_UNKNOWN, out.services[0].load);
    TEST_ASSERT_EQUAL_INT(bus_arrivals::TYPE_UNKNOWN, out.services[0].type);
}

static void test_malformed_eta_leaves_sentinel(void) {
    const char* j = R"({"busStops":[{"BusStopCode":1,
      "UpdatedAt":"2026-05-13T17:47:51+08:00",
      "Services":[{"ServiceNo":"7","NextBus":{
        "EstimatedArrival":"not-a-date","Load":"SEA","Type":"SD"}}]}]})";
    bus_arrivals::BusStopArrivals out{};
    TEST_ASSERT_TRUE(bus_arrivals::parseBusArrivalsJson(j, strlen(j), out));
    TEST_ASSERT_EQUAL_INT32(INT32_MIN, out.services[0].eta_seconds_at_fetch);
}

static void test_empty_busStops_yields_zero_count(void) {
    const char* j = R"({"busStops":[]})";
    bus_arrivals::BusStopArrivals out{};
    TEST_ASSERT_TRUE(bus_arrivals::parseBusArrivalsJson(j, strlen(j), out));
    TEST_ASSERT_EQUAL_UINT8(0, out.service_count);
    TEST_ASSERT_TRUE(out.valid);
}

static void test_truncated_json_returns_error(void) {
    const char* j = R"({"busStops":[{"BusStopCode":1,"Services":[)";
    bus_arrivals::BusStopArrivals out{};
    out.valid = false;
    TEST_ASSERT_FALSE(bus_arrivals::parseBusArrivalsJson(j, strlen(j), out));
    TEST_ASSERT_FALSE(out.valid);
    TEST_ASSERT_NOT_EQUAL(0, out.last_error[0]);
}

static void test_response_too_large_rejected(void) {
    char* big = (char*)calloc(1, bus_arrivals::kMaxResponseBytes + 16);
    TEST_ASSERT_NOT_NULL(big);
    memset(big, 'x', bus_arrivals::kMaxResponseBytes + 8);
    bus_arrivals::BusStopArrivals out{};
    TEST_ASSERT_FALSE(bus_arrivals::parseBusArrivalsJson(
        big, bus_arrivals::kMaxResponseBytes + 8, out));
    TEST_ASSERT_TRUE(strstr(out.last_error, "too large") != nullptr);
    free(big);
}

static void test_service_no_truncated_safely(void) {
    // ServiceNo of "TOOLONG" should be stored as "TOOL" + null.
    const char* j = R"({"busStops":[{"BusStopCode":1,
      "UpdatedAt":"2026-05-13T17:47:51+08:00",
      "Services":[{"ServiceNo":"TOOLONG","NextBus":{
        "EstimatedArrival":"2026-05-13T17:50:00+08:00",
        "Load":"SEA","Type":"SD"}}]}]})";
    bus_arrivals::BusStopArrivals out{};
    TEST_ASSERT_TRUE(bus_arrivals::parseBusArrivalsJson(j, strlen(j), out));
    TEST_ASSERT_EQUAL_size_t(bus_arrivals::kServiceNoLen,
                             strlen(out.services[0].service_no));
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
    RUN_TEST(test_parses_three_services);
    RUN_TEST(test_eta_relative_to_updatedat);
    RUN_TEST(test_truncates_at_max_services);
    RUN_TEST(test_unknown_load_and_type_become_unknown);
    RUN_TEST(test_missing_load_and_type_become_unknown);
    RUN_TEST(test_malformed_eta_leaves_sentinel);
    RUN_TEST(test_empty_busStops_yields_zero_count);
    RUN_TEST(test_truncated_json_returns_error);
    RUN_TEST(test_response_too_large_rejected);
    RUN_TEST(test_service_no_truncated_safely);
    return UNITY_END();
}
