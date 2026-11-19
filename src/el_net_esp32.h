// EchoLink network on the ESP32: direct UDP (5198/5199, needs the ports
// forwarded on the router) or through an EchoLink proxy (one TCP connection).
#pragma once
#include <WiFi.h>
#include <WiFiUdp.h>

#include "echolink.h"
#include "wifi_transport.h"

uint32_t elResolve(const char* host);  // DNS, 0 = failed

class DirectNet : public el::Net {
 public:
  void open(uint32_t nowMs) override;
  void close() override;
  bool ready() override { return open_; }
  void poll(uint32_t nowMs) override;
  bool sendCtrl(uint32_t ip, const uint8_t* d, size_t n) override { return send(ctrl_, ip, 5199, d, n); }
  bool sendAudio(uint32_t ip, const uint8_t* d, size_t n) override { return send(audio_, ip, 5198, d, n); }
  bool dirOpen(uint32_t ip) override;
  bool dirSend(const uint8_t* d, size_t n) override { return dir_.write(d, n) == n; }
  void dirClose() override;
  bool dirConnected() override { return dirOpen_ && dir_.connected(); }
  uint32_t resolve(const char* host) override { return elResolve(host); }
  const char* name() const override { return "UDP"; }

 private:
  bool send(WiFiUDP& u, uint32_t ip, uint16_t port, const uint8_t* d, size_t n);
  void drain(WiFiUDP& u, bool ctrl);
  WiFiUDP audio_, ctrl_;
  WiFiTransport dir_{3000};
  bool open_ = false, dirOpen_ = false;
  uint32_t retryAt_ = 0;
  uint8_t buf_[1500];
};
