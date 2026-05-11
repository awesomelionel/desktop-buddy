#include "github_releases_parse.h"

#include <ArduinoJson.h>
#include <string.h>

namespace github_releases_parse {

namespace {

void copyTruncated(char* dst, size_t dst_len, const char* src) {
    if (!dst || dst_len == 0) return;
    if (!src) { dst[0] = 0; return; }
    size_t n = strnlen(src, dst_len - 1);
    memcpy(dst, src, n);
    dst[n] = 0;
}

void setError(char* err, size_t err_len, const char* msg) {
    if (err && err_len) copyTruncated(err, err_len, msg);
}

}  // namespace

bool parse(const char* json, ReleaseInfo& out, char* err, size_t err_len) {
    if (!json) { setError(err, err_len, "null json"); return false; }

    JsonDocument doc;
    DeserializationError jerr = deserializeJson(doc, json);
    if (jerr) { setError(err, err_len, jerr.c_str()); return false; }

    const char* tag = doc["tag_name"] | (const char*)nullptr;
    if (!tag || !*tag) { setError(err, err_len, "missing tag_name"); return false; }
    if (*tag == 'v' || *tag == 'V') ++tag;
    copyTruncated(out.tag, sizeof(out.tag), tag);

    const char* body = doc["body"] | "";
    copyTruncated(out.body, sizeof(out.body), body);

    out.download_url[0] = 0;
    JsonArray assets = doc["assets"];
    for (JsonObject a : assets) {
        const char* name = a["name"] | "";
        if (strcmp(name, "firmware.bin") == 0) {
            const char* url = a["browser_download_url"] | "";
            copyTruncated(out.download_url, sizeof(out.download_url), url);
            break;
        }
    }
    if (!out.download_url[0]) {
        setError(err, err_len, "no firmware.bin asset");
        return false;
    }

    return true;
}

}  // namespace github_releases_parse
