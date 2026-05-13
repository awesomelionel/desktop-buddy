#include "BusArrivalsFetcher.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include <stdio.h>
#include <string.h>

namespace net {

namespace {
constexpr uint32_t kHttpTimeoutMs = 8000;

void setErr(bus_arrivals::BusStopArrivals& out, const char* msg) {
    size_t cap = sizeof(out.last_error) - 1;
    size_t n = strlen(msg);
    if (n > cap) n = cap;
    memcpy(out.last_error, msg, n);
    out.last_error[n] = '\0';
}
}  // namespace

bool BusArrivalsFetcher::fetch(const char* code, uint32_t now_ms,
                               bus_arrivals::BusStopArrivals& out) {
    if (!code || code[0] == '\0') {
        setErr(out, "no stop code");
        return false;
    }

    Serial.printf("[bus] fetch start code=%s heap_free=%u psram_free=%u "
                  "heap_largest=%u\n",
                  code,
                  (unsigned)ESP.getFreeHeap(),
                  (unsigned)ESP.getFreePsram(),
                  (unsigned)ESP.getMaxAllocHeap());

    NetworkClientSecure client;
    client.setInsecure();   // public, unauthenticated data

    char url[96];
    snprintf(url, sizeof(url),
             "https://api.busaunty.com/api/v1/BusArrival?BusStopCode=%s",
             code);

    HTTPClient http;
    http.setTimeout(kHttpTimeoutMs);
    if (!http.begin(client, url)) {
        Serial.println("[bus] http begin failed");
        setErr(out, "http begin failed");
        return false;
    }
    http.addHeader("User-Agent", "claude-buddy/1");
    http.addHeader("Accept", "application/json");

    int status = http.GET();
    Serial.printf("[bus] GET %s -> %d\n", url, status);
    if (status != 200) {
        char msg[32];
        snprintf(msg, sizeof(msg), "http %d", status);
        setErr(out, msg);
        http.end();
        return false;
    }

    int body_len = http.getSize();   // -1 if chunked
    Serial.printf("[bus] content-length=%d (chunked if -1)\n", body_len);
    if (body_len > (int)bus_arrivals::kMaxResponseBytes) {
        setErr(out, "response too large");
        http.end();
        return false;
    }

    // Let HTTPClient handle chunked decoding and dynamic body sizing. A
    // single-stop response is ~2-3 KB, so the resulting String stays small;
    // pre-allocating a worst-case 32 KB buffer up front fails once TLS
    // scratch buffers have fragmented internal heap below that contiguous
    // size (which is exactly what we observed on device).
    String body = http.getString();
    http.end();
    size_t pos = body.length();
    Serial.printf("[bus] read %u bytes\n", (unsigned)pos);

    if (pos == 0) {
        setErr(out, "empty body");
        return false;
    }
    if (pos > bus_arrivals::kMaxResponseBytes) {
        setErr(out, "response too large");
        return false;
    }

    bool ok = bus_arrivals::parseBusArrivalsJson(body.c_str(), pos, out);
    if (!ok) {
        Serial.printf("[bus] parse failed: %s\n", out.last_error);
        return false;
    }

    Serial.printf("[bus] parse ok: %u services\n", (unsigned)out.service_count);
    out.fetched_at_ms          = now_ms;
    out.last_fetch_success_ms  = now_ms;
    return true;
}

}  // namespace net
