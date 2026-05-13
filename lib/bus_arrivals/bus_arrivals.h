#pragma once

#include <stddef.h>
#include <stdint.h>

namespace bus_arrivals {

constexpr uint8_t  kMaxServicesPerStop = 16;
constexpr uint8_t  kServiceNoLen       = 4;     // "961M\0" needs 5 bytes
constexpr size_t   kMaxResponseBytes   = 32 * 1024;

enum BusLoad : uint8_t { LOAD_UNKNOWN = 0, LOAD_SEA, LOAD_SDA, LOAD_LSD };
enum BusType : uint8_t { TYPE_UNKNOWN = 0, TYPE_SD,  TYPE_DD,  TYPE_BD  };

struct BusServiceArrival {
    char    service_no[kServiceNoLen + 1];   // up to "961M\0"
    int32_t eta_seconds_at_fetch;            // INT32_MIN sentinel = no data
    BusLoad load;
    BusType type;
    bool    wab;                             // parsed but not rendered v1
};

struct BusStopArrivals {
    BusServiceArrival services[kMaxServicesPerStop];
    uint8_t           service_count;
    uint32_t          fetched_at_ms;          // millis() at most recent successful parse
    uint32_t          last_fetch_success_ms;  // == fetched_at_ms; named for staleness checks
    bool              valid;                  // true once at least one fetch has succeeded
    char              last_error[32];         // "" iff most recent attempt succeeded
};

// Parse an ISO-8601 timestamp of the shape "YYYY-MM-DDTHH:MM:SS+HH:MM" (or
// with a fractional seconds component which is ignored) into seconds since
// the Unix epoch. Returns INT32_MIN on any malformed input.
int32_t parseIso8601Delta(const char* iso);

// Parse a complete BusArrival JSON response (shape from
// api.busaunty.com /api/v1/BusArrival?BusStopCode=NNNNN) into out.
// Returns true on success; on failure populates out.last_error and leaves
// out.valid as it was. The first stop in busStops[] is used; additional
// stops are ignored. Caller is responsible for setting out.fetched_at_ms.
bool parseBusArrivalsJson(const char* json, size_t json_len,
                          BusStopArrivals& out);

}  // namespace bus_arrivals
