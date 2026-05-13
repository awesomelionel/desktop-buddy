#include "bus_arrivals.h"

#include <stdio.h>
#include <string.h>

#include <ArduinoJson.h>

namespace {

bool isLeap(int y) {
    return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
}

int32_t toUnix(int y, int mo, int d, int h, int mi, int s) {
    static const int dim[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    int days = 0;
    for (int yr = 1970; yr < y; ++yr) days += isLeap(yr) ? 366 : 365;
    for (int m  = 1;    m  < mo; ++m) {
        days += dim[m-1];
        if (m == 2 && isLeap(y)) days += 1;
    }
    days += (d - 1);
    return (int32_t)days * 86400 + h * 3600 + mi * 60 + s;
}

void setError(bus_arrivals::BusStopArrivals& out, const char* msg) {
    size_t cap = sizeof(out.last_error) - 1;
    size_t n = strlen(msg);
    if (n > cap) n = cap;
    memcpy(out.last_error, msg, n);
    out.last_error[n] = '\0';
}

bus_arrivals::BusLoad parseLoad(const char* s) {
    if (!s) return bus_arrivals::LOAD_UNKNOWN;
    if (strcmp(s, "SEA") == 0) return bus_arrivals::LOAD_SEA;
    if (strcmp(s, "SDA") == 0) return bus_arrivals::LOAD_SDA;
    if (strcmp(s, "LSD") == 0) return bus_arrivals::LOAD_LSD;
    return bus_arrivals::LOAD_UNKNOWN;
}

bus_arrivals::BusType parseType(const char* s) {
    if (!s) return bus_arrivals::TYPE_UNKNOWN;
    if (strcmp(s, "SD") == 0) return bus_arrivals::TYPE_SD;
    if (strcmp(s, "DD") == 0) return bus_arrivals::TYPE_DD;
    if (strcmp(s, "BD") == 0) return bus_arrivals::TYPE_BD;
    return bus_arrivals::TYPE_UNKNOWN;
}

}  // namespace

namespace bus_arrivals {

int32_t parseIso8601Delta(const char* iso) {
    if (!iso) return INT32_MIN;
    // Minimum length: "YYYY-MM-DDTHH:MM:SS+HH:MM" = 25 chars.
    size_t n = strnlen(iso, 64);
    if (n < 25) return INT32_MIN;

    int y, mo, d, h, mi, s;
    int matched = sscanf(iso, "%4d-%2d-%2dT%2d:%2d:%2d",
                         &y, &mo, &d, &h, &mi, &s);
    if (matched != 6) return INT32_MIN;
    if (iso[10] != 'T') return INT32_MIN;

    // Skip an optional ".fff" fractional-seconds component.
    size_t off_pos = 19;
    if (off_pos < n && iso[off_pos] == '.') {
        off_pos++;
        while (off_pos < n && iso[off_pos] >= '0' && iso[off_pos] <= '9') {
            off_pos++;
        }
    }
    if (off_pos >= n) return INT32_MIN;
    char off_sign = iso[off_pos];
    if (off_sign != '+' && off_sign != '-') return INT32_MIN;
    if (off_pos + 6 > n) return INT32_MIN;
    int oh, om;
    if (sscanf(iso + off_pos + 1, "%2d:%2d", &oh, &om) != 2) return INT32_MIN;

    if (mo < 1 || mo > 12 || d < 1 || d > 31) return INT32_MIN;
    if (h  < 0 || h  > 23) return INT32_MIN;
    if (mi < 0 || mi > 59) return INT32_MIN;
    if (s  < 0 || s  > 60) return INT32_MIN;
    if (oh < 0 || oh > 14) return INT32_MIN;
    if (om < 0 || om > 59) return INT32_MIN;

    int32_t local = toUnix(y, mo, d, h, mi, s);
    int32_t off   = oh * 3600 + om * 60;
    if (off_sign == '+') return local - off;
    return local + off;
}

bool parseBusArrivalsJson(const char* json, size_t json_len,
                          BusStopArrivals& out) {
    out.last_error[0] = '\0';
    if (json_len > kMaxResponseBytes) {
        setError(out, "response too large");
        return false;
    }
    if (!json || json_len == 0) {
        setError(out, "empty response");
        return false;
    }

    // Filter: only materialize the fields we render. This bounds parse
    // memory regardless of how many NextBus2/NextBus3/Feature/UpdatedAt
    // entries the server sends.
    JsonDocument filter;
    filter["busStops"][0]["BusStopCode"]                        = true;
    filter["busStops"][0]["UpdatedAt"]                          = true;
    filter["busStops"][0]["Services"][0]["ServiceNo"]           = true;
    filter["busStops"][0]["Services"][0]["NextBus"]["EstimatedArrival"] = true;
    filter["busStops"][0]["Services"][0]["NextBus"]["Load"]     = true;
    filter["busStops"][0]["Services"][0]["NextBus"]["Type"]     = true;
    filter["busStops"][0]["Services"][0]["NextBus"]["Feature"]  = true;

    JsonDocument doc;
    DeserializationError err = deserializeJson(
        doc, json, json_len, DeserializationOption::Filter(filter));
    if (err == DeserializationError::NoMemory) {
        setError(out, "json too big");
        return false;
    }
    if (err) {
        setError(out, err.c_str());
        return false;
    }

    JsonArrayConst stops = doc["busStops"].as<JsonArrayConst>();
    if (stops.size() == 0) {
        out.service_count = 0;
        out.valid = true;
        return true;
    }
    JsonObjectConst stop = stops[0].as<JsonObjectConst>();

    int32_t updated_unix = parseIso8601Delta(stop["UpdatedAt"] | "");
    JsonArrayConst svcs = stop["Services"].as<JsonArrayConst>();

    out.service_count = 0;
    for (JsonObjectConst svc : svcs) {
        if (out.service_count >= kMaxServicesPerStop) break;
        BusServiceArrival& dst = out.services[out.service_count++];

        const char* sn = svc["ServiceNo"] | "";
        size_t snn = strnlen(sn, kServiceNoLen + 8);
        if (snn > kServiceNoLen) snn = kServiceNoLen;
        memcpy(dst.service_no, sn, snn);
        dst.service_no[snn] = '\0';

        JsonObjectConst nb = svc["NextBus"].as<JsonObjectConst>();
        const char* iso = nb["EstimatedArrival"] | "";
        int32_t arr = parseIso8601Delta(iso);
        if (arr == INT32_MIN || updated_unix == INT32_MIN) {
            dst.eta_seconds_at_fetch = INT32_MIN;
        } else {
            dst.eta_seconds_at_fetch = arr - updated_unix;
        }
        dst.load = parseLoad(nb["Load"] | (const char*)nullptr);
        dst.type = parseType(nb["Type"] | (const char*)nullptr);
        const char* feat = nb["Feature"] | "";
        dst.wab = (strcmp(feat, "WAB") == 0);
    }

    out.valid = true;
    return true;
}

}  // namespace bus_arrivals
