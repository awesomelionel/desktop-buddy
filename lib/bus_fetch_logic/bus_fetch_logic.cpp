#include "bus_fetch_logic.h"

#include <string.h>

namespace bus_fetch_logic {

int pickHighestPriority(const SlotRequest (&table)[settings::MAX_BUS_STOPS]) {
    int best = -1;
    for (size_t i = 0; i < settings::MAX_BUS_STOPS; ++i) {
        if (!table[i].wanted) continue;
        // Strict > on prio means equal priorities keep the earlier (lower-
        // index) slot we already chose, which is the deterministic tie rule.
        if (best < 0 || table[i].prio > table[best].prio) {
            best = (int)i;
        }
    }
    return best;
}

void applyRequest(SlotRequest& entry, const char* code,
                  FetchPriority new_prio) {
    // Always refresh the code. strnlen + bounded memcpy + explicit null term
    // — defensive even though caller is expected to pass a valid stop code.
    size_t n = strnlen(code, settings::BUS_STOP_CODE_LEN);
    memcpy(entry.code, code, n);
    entry.code[n] = '\0';

    // Priority upgrades but never downgrades while still wanted.
    if (!entry.wanted || new_prio > entry.prio) {
        entry.prio = new_prio;
    }
    entry.wanted = true;
}

}  // namespace bus_fetch_logic
