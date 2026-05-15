#pragma once

#include <stdint.h>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include "BusArrivalsFetcher.h"
#include "bus_arrivals.h"
#include "bus_fetch_logic.h"
#include "settings_model.h"

namespace net {

// Single shared bus-arrivals fetch worker. One persistent FreeRTOS task
// drains a 4-entry request table (one per bus stop slot) and stages
// results into a 4-entry buffer that BusCard polls via takeResult().
//
// All public methods are called from the main thread only.
class BusFetchService {
public:
    BusFetchService();
    ~BusFetchService();

    // Create the worker task. Returns true on success; false leaves the
    // service in synchronous-fallback mode (request() runs the fetch
    // inline on the caller's thread).
    bool begin();

    // True iff the worker task is running. False means synchronous fallback.
    bool isAsync() const { return async_; }

    // Submit (or refresh) a fetch for slot. Non-blocking in async mode.
    // code is copied immediately under the mutex (no Settings race).
    // Empty code or out-of-range slot is a silent no-op.
    void request(uint8_t slot, const char* code,
                 bus_fetch_logic::FetchPriority priority);

    // If a fresh result is staged for slot, copy it into out, clear the
    // staged flag, return true. Else return false. Idempotent.
    bool takeResult(uint8_t slot, bus_arrivals::BusStopArrivals& out);

private:
    static void workerEntry(void* arg);
    void        workerLoop();
    void        runFetchSync(uint8_t slot, const char* code);

    bus_fetch_logic::SlotRequest  requests_[settings::MAX_BUS_STOPS];
    bus_arrivals::BusStopArrivals staging_[settings::MAX_BUS_STOPS];
    bool                          staged_ready_[settings::MAX_BUS_STOPS];

    BusArrivalsFetcher  fetcher_;       // used only on the worker thread
    SemaphoreHandle_t   mutex_;         // guards requests_ + staging_ + staged_ready_
    SemaphoreHandle_t   wake_;          // binary sem: main signals a new request
    TaskHandle_t        task_;
    bool                async_;
    bool                logged_high_water_;   // one-shot "first fetch" stack log
};

}  // namespace net
