#include "protocol.h"
#include <ArduinoJson.h>
#include <string.h>

bool protocol_parse_line(const char* line, ClaudeStatus* out) {
    if (!line || !out) return false;
    if (line[0] != '{') return false;

    JsonDocument doc;
    if (deserializeJson(doc, line)) return false;
    if (!doc.is<JsonObject>()) return false;

    out->total   = doc["total"]   | out->total;
    out->running = doc["running"] | out->running;
    out->waiting = doc["waiting"] | out->waiting;

    const char* m = doc["msg"];
    if (m) {
        strncpy(out->msg, m, sizeof(out->msg) - 1);
        out->msg[sizeof(out->msg) - 1] = 0;
    }
    out->valid = true;
    return true;
}
