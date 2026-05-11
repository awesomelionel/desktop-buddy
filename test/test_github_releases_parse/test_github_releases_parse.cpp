#include <unity.h>
#include <stdio.h>
#include <string.h>
#include "github_releases_parse.h"

void setUp(void) {}
void tearDown(void) {}

static void test_parses_happy_path(void) {
    const char* json = R"({
        "tag_name": "v1.2.3",
        "body": "release notes here",
        "draft": false,
        "prerelease": false,
        "assets": [
            {"name": "firmware.bin",
             "browser_download_url": "https://example.com/firmware.bin"}
        ]
    })";

    github_releases_parse::ReleaseInfo info{};
    char err[64] = {};
    TEST_ASSERT_TRUE(github_releases_parse::parse(json, info, err, sizeof(err)));
    TEST_ASSERT_EQUAL_STRING("1.2.3", info.tag);
    TEST_ASSERT_EQUAL_STRING("release notes here", info.body);
    TEST_ASSERT_EQUAL_STRING("https://example.com/firmware.bin",
                             info.download_url);
}

static void test_strips_v_prefix(void) {
    const char* json = R"({
        "tag_name": "v0.0.1",
        "body": "",
        "draft": false, "prerelease": false,
        "assets": [{"name": "firmware.bin",
                    "browser_download_url": "https://x/firmware.bin"}]
    })";
    github_releases_parse::ReleaseInfo info{};
    char err[64] = {};
    TEST_ASSERT_TRUE(github_releases_parse::parse(json, info, err, sizeof(err)));
    TEST_ASSERT_EQUAL_STRING("0.0.1", info.tag);
}

static void test_keeps_tag_without_v_prefix(void) {
    const char* json = R"({
        "tag_name": "1.5.0",
        "body": "",
        "draft": false, "prerelease": false,
        "assets": [{"name": "firmware.bin",
                    "browser_download_url": "https://x/firmware.bin"}]
    })";
    github_releases_parse::ReleaseInfo info{};
    char err[64] = {};
    TEST_ASSERT_TRUE(github_releases_parse::parse(json, info, err, sizeof(err)));
    TEST_ASSERT_EQUAL_STRING("1.5.0", info.tag);
}

static void test_rejects_no_firmware_asset(void) {
    const char* json = R"({
        "tag_name": "v1.0.0",
        "body": "",
        "draft": false, "prerelease": false,
        "assets": [{"name": "source.zip",
                    "browser_download_url": "https://x/source.zip"}]
    })";
    github_releases_parse::ReleaseInfo info{};
    char err[64] = {};
    TEST_ASSERT_FALSE(github_releases_parse::parse(json, info, err, sizeof(err)));
    TEST_ASSERT_TRUE(strstr(err, "firmware.bin") != nullptr);
}

static void test_rejects_empty_assets(void) {
    const char* json = R"({
        "tag_name": "v1.0.0",
        "body": "",
        "draft": false, "prerelease": false,
        "assets": []
    })";
    github_releases_parse::ReleaseInfo info{};
    char err[64] = {};
    TEST_ASSERT_FALSE(github_releases_parse::parse(json, info, err, sizeof(err)));
}

static void test_rejects_malformed_json(void) {
    const char* json = "{not even json";
    github_releases_parse::ReleaseInfo info{};
    char err[64] = {};
    TEST_ASSERT_FALSE(github_releases_parse::parse(json, info, err, sizeof(err)));
}

static void test_truncates_long_body(void) {
    // Build a body string longer than the buffer.
    char json[2048];
    char long_body[1500];
    for (size_t i = 0; i < sizeof(long_body) - 1; ++i) long_body[i] = 'x';
    long_body[sizeof(long_body) - 1] = 0;
    snprintf(json, sizeof(json),
             "{\"tag_name\":\"v1.0.0\",\"body\":\"%s\","
             "\"draft\":false,\"prerelease\":false,"
             "\"assets\":[{\"name\":\"firmware.bin\","
             "\"browser_download_url\":\"https://x/firmware.bin\"}]}",
             long_body);

    github_releases_parse::ReleaseInfo info{};
    char err[64] = {};
    TEST_ASSERT_TRUE(github_releases_parse::parse(json, info, err, sizeof(err)));
    // body field is bounded; we just confirm it doesn't overflow.
    TEST_ASSERT_TRUE(strlen(info.body) < sizeof(info.body));
}

int main(int /*argc*/, char** /*argv*/) {
    UNITY_BEGIN();
    RUN_TEST(test_parses_happy_path);
    RUN_TEST(test_strips_v_prefix);
    RUN_TEST(test_keeps_tag_without_v_prefix);
    RUN_TEST(test_rejects_no_firmware_asset);
    RUN_TEST(test_rejects_empty_assets);
    RUN_TEST(test_rejects_malformed_json);
    RUN_TEST(test_truncates_long_body);
    return UNITY_END();
}
