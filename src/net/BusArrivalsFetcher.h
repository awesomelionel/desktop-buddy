#pragma once

#include <stdint.h>

#include "bus_arrivals.h"

namespace net {

// Synchronous fetch of one bus stop's arrivals from api.busaunty.com.
// HTTPS via NetworkClientSecure with setInsecure() — the data is public
// and unauthenticated, so a CA bundle is intentionally not used.
//
// On success: out.valid = true, out.service_count populated, out.fetched_at_ms
// and out.last_fetch_success_ms set to now_ms, out.last_error = "".
// On failure: out.last_error populated; out.valid is left as it was so callers
// can detect "stale data with prior values" vs "never had data".
//
// Bounds: response body capped at bus_arrivals::kMaxResponseBytes; HTTP
// timeout 8 s; service count capped at bus_arrivals::kMaxServicesPerStop.
class BusArrivalsFetcher {
public:
    // code is the 5-digit Singapore stop code, null-terminated.
    bool fetch(const char* code, uint32_t now_ms,
               bus_arrivals::BusStopArrivals& out);
};

}  // namespace net
