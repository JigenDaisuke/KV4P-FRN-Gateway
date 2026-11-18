#include "logbuf.h"

#include <stdarg.h>

namespace {
constexpr int LINES = 60;
constexpr int LEN = 120;
char ring[LINES][LEN];
int head = 0, count = 0;
SemaphoreHandle_t mtx = nullptr;
}  // namespace

namespace logbuf {

void begin() { mtx = xSemaphoreCreateMutex(); }

void add(const char* fmt, ...) {
  char line[LEN];
  uint32_t s = millis() / 1000;
  int n = snprintf(line, sizeof(line), "%02lu:%02lu:%02lu ", (unsigned long)(s / 3600),
                   (unsigned long)((s / 60) % 60), (unsigned long)(s % 60));
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(line + n, sizeof(line) - n, fmt, ap);
  va_end(ap);
  Serial.println(line);
  if (!mtx) return;
  xSemaphoreTake(mtx, portMAX_DELAY);
  strncpy(ring[head], line, LEN - 1);
  ring[head][LEN - 1] = 0;
  head = (head + 1) % LINES;
  if (count < LINES) count++;
  xSemaphoreGive(mtx);
}

void dump(String& out) {
  if (!mtx) return;
  xSemaphoreTake(mtx, portMAX_DELAY);
  int start = (head - count + LINES) % LINES;
  for (int i = 0; i < count; i++) {
    out += ring[(start + i) % LINES];
    out += '\n';
  }
  xSemaphoreGive(mtx);
}

}  // namespace logbuf
