// SA818 / DRA818 control over UART + PTT/PD/SQ lines.
#pragma once
#include <Arduino.h>

#include "settings.h"

class RadioSA818 {
 public:
  bool begin(HardwareSerial& port);      // power up + handshake
  bool configure(const Settings& s);     // frequency, tones, squelch, volume, filters
  void setPtt(bool transmit);
  // Applies settings.highPower according to settings.powerPinMode.
  // Returns false when the H/L pin is not wired (power fixed by hardware).
  bool applyPower(const Settings& s);
  bool ptt() const { return ptt_; }
  // Hardware squelch: true when the module reports a signal.
  bool carrier() const { return digitalRead(board_sq_) == LOW; }
  bool found() const { return found_; }
  const String& lastReply() const { return reply_; }

 private:
  bool command(const char* cmd, const char* expectPrefix, uint32_t timeoutMs = 1200);
  HardwareSerial* ser_ = nullptr;
  bool ptt_ = false;
  bool found_ = false;
  int board_sq_ = 32;
  String reply_;
};
