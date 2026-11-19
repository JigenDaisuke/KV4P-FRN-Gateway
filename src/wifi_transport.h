// frn::Transport on top of WiFiClient (used by FRN, the EchoLink directory
// connection and the EchoLink proxy).
#pragma once
#include <WiFi.h>

#include "frn_client.h"

class WiFiTransport : public frn::Transport {
 public:
  bool connect(const char* host, uint16_t port) override {
    c_.stop();
    if (!c_.connect(host, port, 5000)) return false;
    c_.setNoDelay(true);
    return true;
  }
  void close() override { c_.stop(); }
  bool connected() override { return c_.connected(); }
  int read(uint8_t* buf, size_t maxLen) override {
    int a = c_.available();
    if (a <= 0) return 0;
    return c_.read(buf, (size_t)a < maxLen ? (size_t)a : maxLen);
  }
  size_t write(const uint8_t* buf, size_t len) override { return c_.write(buf, len); }

 private:
  WiFiClient c_;
};
