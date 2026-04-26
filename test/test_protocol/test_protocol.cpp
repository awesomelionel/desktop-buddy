#include <unity.h>
#include <string.h>
#include "protocol.h"

void setUp(void) {}
void tearDown(void) {}

static void test_parse_full_snapshot(void) {
    ClaudeStatus s = {};
    bool ok = protocol_parse_line(
        "{\"total\":3,\"running\":1,\"waiting\":2,\"msg\":\"approve: Bash\"}", &s);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_TRUE(s.valid);
    TEST_ASSERT_EQUAL_UINT8(3, s.total);
    TEST_ASSERT_EQUAL_UINT8(1, s.running);
    TEST_ASSERT_EQUAL_UINT8(2, s.waiting);
    TEST_ASSERT_EQUAL_STRING("approve: Bash", s.msg);
}

static void test_parse_missing_fields_keeps_previous(void) {
    ClaudeStatus s = {};
    s.total = 5; s.running = 2; s.waiting = 1;
    strcpy(s.msg, "old");
    bool ok = protocol_parse_line("{\"running\":4}", &s);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_UINT8(5, s.total);     // preserved
    TEST_ASSERT_EQUAL_UINT8(4, s.running);   // updated
    TEST_ASSERT_EQUAL_UINT8(1, s.waiting);   // preserved
    TEST_ASSERT_EQUAL_STRING("old", s.msg);  // preserved
}

static void test_parse_rejects_non_object(void) {
    ClaudeStatus s = {};
    TEST_ASSERT_FALSE(protocol_parse_line("not json", &s));
    TEST_ASSERT_FALSE(protocol_parse_line("[1,2,3]", &s));
    TEST_ASSERT_FALSE(protocol_parse_line("", &s));
    TEST_ASSERT_FALSE(protocol_parse_line(nullptr, &s));
    TEST_ASSERT_FALSE(s.valid);
}

static void test_parse_rejects_malformed_json(void) {
    ClaudeStatus s = {};
    TEST_ASSERT_FALSE(protocol_parse_line("{not json", &s));
    TEST_ASSERT_FALSE(s.valid);
}

static void test_parse_truncates_long_msg(void) {
    ClaudeStatus s = {};
    bool ok = protocol_parse_line(
        "{\"msg\":\"this string is intentionally longer than the buffer fits\"}", &s);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_size_t(sizeof(s.msg) - 1, strlen(s.msg));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_parse_full_snapshot);
    RUN_TEST(test_parse_missing_fields_keeps_previous);
    RUN_TEST(test_parse_rejects_non_object);
    RUN_TEST(test_parse_rejects_malformed_json);
    RUN_TEST(test_parse_truncates_long_msg);
    return UNITY_END();
}
