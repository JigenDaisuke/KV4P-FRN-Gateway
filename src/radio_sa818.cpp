#include "radio_sa818.h"

#include "board_kv4p.h"

bool RadioSA818::begin(HardwareSerial& port) {
  ser_ = &port;
  board_sq_ = board::PIN_SQ;
  pinMode(board::PIN_PTT, OUTPUT);
  digitalWrite(board::PIN_PTT, HIGH);  // receive
  pinMode(board::PIN_PD, OUTPUT);
  digitalWrite(board::PIN_PD, HIGH);   // power on
  pinMode(board::PIN_SQ, INPUT);
  ser_->begin(board::SA818_BAUD, SERIAL_8N1, board::PIN_RF_RXD, board::PIN_RF_TXD);
  delay(500);  // module boot

  found_ = false;
  for (int i = 0; i < 6 && !found_; i++) {
    found_ = command("AT+DMOCONNECT\r\n", "+DMOCONNECT:0");
    if (!found_) delay(1000);
  }
  return found_;
}

bool RadioSA818::command(const char* cmd, const char* expectPrefix, uint32_t timeoutMs) {
  while (ser_->available()) ser_->read();  // flush
  ser_->print(cmd);
  reply_ = "";
  uint32_t t0 = millis();
  while (millis() - t0 < timeoutMs) {
    while (ser_->available()) {
      char c = (char)ser_->read();
      if (c == '\n') {
        reply_.trim();
        if (reply_.startsWith(expectPrefix)) return true;
        // Some firmware revisions answer with a slightly different prefix:
        // accept any "+XXX:0" as success, "+XXX:1" as failure.
        if (reply_.startsWith("+")) return reply_.endsWith(":0");
        reply_ = "";
      } else if (c != '\r') {
        reply_ += c;
      }
    }
    delay(2);
  }
  return false;
}

bool RadioSA818::configure(const Settings& s) {
  if (!ser_) return false;
  char buf[64];
  // Squelch 0 would keep the SQ line always "open": force at least 1.
  uint8_t sq = s.squelch < 1 ? 1 : (s.squelch > 8 ? 8 : s.squelch);
  snprintf(buf, sizeof(buf), "AT+DMOSETGROUP=%u,%.4f,%.4f,%04u,%u,%04u\r\n", (unsigned)(s.bandwidth ? 1 : 0),
           (double)s.freqTx, (double)s.freqRx, (unsigned)s.ctcssTx, (unsigned)sq, (unsigned)s.ctcssRx);
  bool ok = false;
  for (int i = 0; i < 3 && !ok; i++) ok = command(buf, "+DMOSETGROUP:0");

  uint8_t vol = s.volume < 1 ? 1 : (s.volume > 8 ? 8 : s.volume);
  snprintf(buf, sizeof(buf), "AT+DMOSETVOLUME=%u\r\n", (unsigned)vol);
  bool okv = command(buf, "+DMOSETVOLUME:0");

  // AT+SETFILTER=emph,highpass,lowpass : 0 = filter active, 1 = bypass
  snprintf(buf, sizeof(buf), "AT+SETFILTER=%d,%d,%d\r\n", s.filterEmphasis ? 0 : 1, s.filterHighpass ? 0 : 1,
           s.filterLowpass ? 0 : 1);
  bool okf = command(buf, "+DMOSETFILTER:0");
  return ok && okv && okf;
}

bool RadioSA818::applyPower(const Settings& s) {
  switch (s.powerPinMode) {
    case 1:  // straight wire to the module: pull LOW for low power, leave open for high
      if (s.highPower) {
        pinMode(board::PIN_HL, INPUT);
      } else {
        pinMode(board::PIN_HL, OUTPUT);
        digitalWrite(board::PIN_HL, LOW);
      }
      return true;
    case 2:  // KV4P v2.0: transistor driver, inverted
      pinMode(board::PIN_HL, OUTPUT);
      digitalWrite(board::PIN_HL, s.highPower ? LOW : HIGH);
      return true;
    default:
      return false;  // H/L not connected: module at its default (high) power
  }
}

void RadioSA818::setPtt(bool transmit) {
  ptt_ = transmit;
  digitalWrite(board::PIN_PTT, transmit ? LOW : HIGH);
}
