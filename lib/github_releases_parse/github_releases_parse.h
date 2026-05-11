#pragma once

#include <stddef.h>

namespace github_releases_parse {

struct ReleaseInfo {
    char tag[16];               // base version without leading 'v'
    char body[1024];            // release notes; truncated to buffer
    char download_url[256];     // first asset named "firmware.bin"
};

// Parses a GitHub /releases/latest response body into `out`. Returns
// true on success; on failure writes a short reason into `err` and
// returns false. `out` is unspecified on failure (caller should treat
// it as invalid).
bool parse(const char* json, ReleaseInfo& out, char* err, size_t err_len);

}  // namespace github_releases_parse
