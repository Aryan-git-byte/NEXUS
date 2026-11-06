// Windows-only clock compatibility for the headless UI test. Product firmware
// uses ESP-IDF's POSIX clock APIs. The renderer never changes the PC clock.
#pragma once
#include_next <sys/time.h>
#if defined(_WIN32)
#include <ctime>

inline tm* gmtime_r(const time_t* value, tm* output) {
    const tm* result = std::gmtime(value);
    if (!result) return nullptr;
    *output = *result;
    return output;
}

inline int settimeofday(const timeval*, const void*) { return 0; }
#endif
