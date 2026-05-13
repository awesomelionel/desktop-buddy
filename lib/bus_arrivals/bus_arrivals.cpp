#include "bus_arrivals.h"

#include <stdio.h>
#include <string.h>

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

bool parseBusArrivalsJson(const char* /*json*/, size_t /*json_len*/,
                          BusStopArrivals& /*out*/) {
    return false;       // stub — Task 6 fills this in
}

}  // namespace bus_arrivals
