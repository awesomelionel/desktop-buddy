#pragma once

#include <stddef.h>

#include <ArduinoJson.h>

namespace github_releases_parse {

struct ReleaseInfo {
    char tag[16];               // base version without leading 'v'
    char body[1024];            // release notes; truncated to buffer
    char download_url[256];     // first asset named "firmware.bin"
};

// Extracts the ReleaseInfo fields from an already-deserialized
// JsonDocument. Returns true on success; on failure writes a short
// reason into `err`.
bool parseFromDocument(const JsonDocument& doc, ReleaseInfo& out,
                       char* err, size_t err_len);

// Parses a GitHub /releases/latest JSON string into `out`. Convenience
// wrapper around deserializeJson + parseFromDocument. Returns true on
// success.
bool parse(const char* json, ReleaseInfo& out, char* err, size_t err_len);

}  // namespace github_releases_parse
