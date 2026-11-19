#include "el_net_esp32.h"

uint32_t elResolve(const char* host) {
  IPAddress ip;
  if (!WiFi.hostByName(host, ip)) return 0;
  return ((uint32_t)ip[0] << 24) | ((uint32_t)ip[1] << 16) | ((uint32_t)ip[2] << 8) | ip[3];
}

static IPAddress toIp(uint32_t ip) { return IPAddress((uint8_t)(ip >> 24), (uint8_t)(ip >> 16), (uint8_t)(ip >> 8), (uint8_t)ip); }

void DirectNet::open(uint32_t nowMs) {
  if (open_ || (int32_t)(nowMs - retryAt_) < 0) return;
  retryAt_ = nowMs + 10000;
  open_ = audio_.begin(5198) && ctrl_.begin(5199);
  if (onLog) onLog(open_ ? "UDP 5198/5199 open (forward them on the router for incoming calls)" : "cannot open UDP ports");
}

void DirectNet::close() {
  audio_.stop();
  ctrl_.stop();
  dirClose();
  open_ = false;
}

bool DirectNet::send(WiFiUDP& u, uint32_t ip, uint16_t port, const uint8_t* d, size_t n) {
  if (!open_ || !ip) return false;
  if (!u.beginPacket(toIp(ip), port)) return false;
  u.write(d, n);
  return u.endPacket() == 1;
}

void DirectNet::drain(WiFiUDP& u, bool ctrl) {
  for (int i = 0; i < 8; i++) {
    int n = u.parsePacket();
    if (n <= 0) break;
    IPAddress r = u.remoteIP();
    uint32_t ip = ((uint32_t)r[0] << 24) | ((uint32_t)r[1] << 16) | ((uint32_t)r[2] << 8) | r[3];
    int got = u.read(buf_, sizeof(buf_));
    if (got <= 0) continue;
    if (ctrl) {
      if (onCtrl) onCtrl(ip, buf_, (size_t)got);
    } else {
      if (onAudio) onAudio(ip, buf_, (size_t)got);
    }
  }
}

void DirectNet::poll(uint32_t nowMs) {
  (void)nowMs;
  if (!open_) return;
  drain(ctrl_, true);
  drain(audio_, false);
  if (dirOpen_) {
    bool more = false;
    for (int i = 0; i < 16 && dirOpen_; i++) {
      int r = dir_.read(buf_, sizeof(buf_));
      if (r <= 0) {
        more = false;
        break;
      }
      more = true;
      if (onDir) onDir(buf_, (size_t)r);
    }
    // The server closes right after its answer: report the close only once
    // everything it sent has been read (connected() turns false earlier).
    if (dirOpen_ && !more && !dir_.connected()) {
      dirOpen_ = false;
      if (onDirClosed) onDirClosed();
    }
  }
}

bool DirectNet::dirOpen(uint32_t ip) {
  dirOpen_ = dir_.connect(el::ipToString(ip).c_str(), 5200);
  if (!dirOpen_ && onLog) {
    std::string m = "cannot connect to the directory " + el::ipToString(ip) + ":5200";
    onLog(m.c_str());
  }
  return dirOpen_;
}

void DirectNet::dirClose() {
  if (dirOpen_) dir_.close();
  dirOpen_ = false;
}
