// Tiny thread-safe log: Serial + ring buffer shown on the web page.
#pragma once
#include <Arduino.h>

namespace logbuf {
void begin();
void add(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
// Appends the buffered lines (oldest first) to out.
void dump(String& out);
}  // namespace logbuf

#define LOGF(...) logbuf::add(__VA_ARGS__)
