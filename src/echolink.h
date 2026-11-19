// EchoLink node client (directory registration, station lookup, one QSO at a
// time, GSM audio), direct UDP or through an EchoLink proxy.
//
// Protocol behaviour follows SvxLink's echolib (GPL, (C) Tobias Blomberg
// SM0SVX): EchoLinkDirectory, EchoLinkQso, EchoLinkProxy, rtpacket.
//
//  directory  TCP 5200, one connection per command:
//             "l" CALL "\254\254" PASS "\rONLINE3.38(HH:MM)\r" LOCATION "\r"  -> "OK"
//             "s" -> "@@@\n" count "\n" { call "\n" data "\n" id "\n" ip "\n" } "+++"
//  control    UDP 5199: RTCP RR+SDES (call/name), every 10 s; RR+BYE to hang up
//  audio      UDP 5198: RTP 0xC0 0x03 seq time ssrc + 4 x 33-byte GSM frames (80 ms);
//             "oNDATA\r<info>\0" info text, "oNDATA<call>>text\r\n\0" chat
//  proxy      TCP (8100): nonce(8) -> CALL "\n" MD5(PASSWORD+nonce); then blocks
//             [type][ip a.b.c.d][len LE32][data], types: 1 TCP_OPEN 2 TCP_DATA
//             3 TCP_CLOSE 4 TCP_STATUS 5 UDP_DATA 6 UDP_CONTROL 7 SYSTEM
#pragma once
#include <stddef.h>
#include <stdint.h>

#include <functional>
#include <string>
#include <vector>

#include "frn_client.h"  // frn::Transport, frn::Listener

namespace el {

constexpr int GSM_FRAME_BYTES = 33;
constexpr int FRAMES_PER_PACKET = 4;
constexpr int PACKET_SAMPLES = FRAMES_PER_PACKET * 160;  // 640 = 80 ms
constexpr int RTP_HEADER = 12;

// IPv4 as a.b.c.d -> (a << 24) | (b << 16) | (c << 8) | d
std::string ipToString(uint32_t ip);

struct Config {
  std::string callsign;               // e.g. "IU8UUT-L"
  std::string password;
  std::string sysopName;              // shown to connected stations
  std::string location;               // directory description (max 27 chars)
  std::string info = "KV4P ESP32 FRN/EchoLink gateway - FRN ITALIA";
  std::string server = "servers.echolink.org";
  std::string proxyHost;              // empty = direct UDP; several proxies: "a.b.c, d.e.f" (tried in turn)
  uint16_t proxyPort = 8100;
  std::string proxyPassword = "PUBLIC";
  bool acceptIncoming = true;
  uint32_t idleTimeoutS = 600;        // hang up after this long without audio (0 = never)
};

// Datagrams + directory TCP, implemented directly or through a proxy.
class Net {
 public:
  virtual ~Net() {}
  virtual void open(uint32_t nowMs) = 0;
  virtual void close() = 0;
  virtual bool ready() = 0;
  virtual void poll(uint32_t nowMs) = 0;
  virtual bool sendCtrl(uint32_t ip, const uint8_t* d, size_t n) = 0;
  virtual bool sendAudio(uint32_t ip, const uint8_t* d, size_t n) = 0;
  virtual bool dirOpen(uint32_t ip) = 0;
  virtual bool dirSend(const uint8_t* d, size_t n) = 0;
  virtual void dirClose() = 0;
  virtual bool dirConnected() = 0;
  virtual uint32_t resolve(const char* host) = 0;  // 0 = failed
  virtual const char* name() const = 0;

  std::function<void(uint32_t ip, const uint8_t*, size_t)> onCtrl, onAudio;
  std::function<void(const uint8_t*, size_t)> onDir;
  std::function<void()> onDirClosed;
  std::function<void(const char*)> onLog;
};

// EchoLink proxy over any byte stream (portable).
class ProxyNet : public Net {
 public:
  ProxyNet(frn::Transport& tcp, std::function<uint32_t(const char*)> resolver) : tcp_(tcp), resolver_(resolver) {}
  // hosts: one or more proxies separated by commas/spaces; on failure the next one is tried
  void configure(const std::string& hosts, uint16_t port, const std::string& callsign, const std::string& password);
  const std::string& currentHost() const { return host_; }
  void open(uint32_t nowMs) override;
  void close() override;
  bool ready() override { return state_ == READY; }
  void poll(uint32_t nowMs) override;
  bool sendCtrl(uint32_t ip, const uint8_t* d, size_t n) override { return block(6, ip, d, n); }
  bool sendAudio(uint32_t ip, const uint8_t* d, size_t n) override { return block(5, ip, d, n); }
  bool dirOpen(uint32_t ip) override;
  bool dirSend(const uint8_t* d, size_t n) override { return dirState_ == 2 && block(2, 0, d, n); }
  void dirClose() override;
  bool dirConnected() override { return dirState_ == 2; }
  uint32_t resolve(const char* host) override { return resolver_ ? resolver_(host) : 0; }
  const char* name() const override { return name_.c_str(); }

 private:
  bool block(uint8_t type, uint32_t ip, const uint8_t* d, size_t n);
  void handle(uint8_t type, uint32_t ip, const uint8_t* d, size_t n);
  void log(const char* m) {
    if (onLog) onLog(m);
  }
  void nextHost(uint32_t nowMs, const char* why);
  std::vector<std::string> hosts_;
  size_t idx_ = 0;
  uint32_t lastNow_ = 0;
  enum { CLOSED, WAIT_NONCE, READY } state_ = CLOSED;
  frn::Transport& tcp_;
  std::function<uint32_t(const char*)> resolver_;
  std::string host_, call_, pass_;
  std::string name_ = "proxy";
  uint16_t port_ = 8100;
  int dirState_ = 0;  // 0 closed, 1 opening, 2 open
  uint32_t retryAt_ = 0;
  uint8_t buf_[2048];
  size_t len_ = 0;
};

class Client {
 public:
  static constexpr uint32_t REGISTER_EVERY_MS = 5 * 60 * 1000;
  static constexpr uint32_t DIR_TIMEOUT_MS = 30000;
  static constexpr uint32_t KEEPALIVE_MS = 10000;
  static constexpr uint32_t CON_TIMEOUT_MS = 50000;
  static constexpr int CONNECT_RETRIES = 5;
  static constexpr uint32_t RX_END_MS = 400;

  Client(Net& net, frn::Listener& li);
  ~Client();
  void setConfig(const Config& c);
  const Config& config() const { return cfg_; }
  // Local time "HH:MM" for the directory (platform provided).
  std::function<std::string()> localTime;
  // Results / state changes for the gateway.
  std::function<void(bool ok, const std::string& call)> onConnectResult;
  std::function<void(bool connected, const std::string& call)> onQso;

  void start(uint32_t nowMs);
  void stop(uint32_t nowMs);
  void loop(uint32_t nowMs);

  // Connect to a node number (directory lookup, then SDES).
  bool connectTo(uint32_t nodeId, uint32_t nowMs);
  void disconnect(uint32_t nowMs, const char* why);
  bool sendVoice(const int16_t* pcm640, uint32_t nowMs);

  bool running() const { return running_; }
  bool registered() const { return registered_; }
  bool connected() const { return qso_ == QSO_CONNECTED; }
  bool busy() const { return qso_ != QSO_IDLE || lookupId_ != 0; }
  const std::string& remoteCall() const { return rcall_; }
  const char* stateName() const;

  // Packet helpers (also used by the tests)
  static size_t makeSdes(uint8_t* p, const std::string& call, const std::string& name);
  static size_t makeBye(uint8_t* p);
  static bool isSdes(const uint8_t* p, size_t n);
  static bool isBye(const uint8_t* p, size_t n);
  static bool sdesName(const uint8_t* p, size_t n, std::string& out);

 private:
  enum Qso { QSO_IDLE, QSO_CONNECTING, QSO_CONNECTED };
  enum DirJob { DJ_NONE, DJ_REGISTER, DJ_LOOKUP };
  enum DirStep { DS_CONNECTING, DS_WAIT_REPLY };

  void log(const char* fmt, ...);
  void handleCtrl(uint32_t ip, const uint8_t* d, size_t n);
  void handleAudio(uint32_t ip, const uint8_t* d, size_t n);
  void handleDir(const uint8_t* d, size_t n);
  void handleDirLine(const std::string& line);
  void dirDone(bool ok);
  void startDirJob(DirJob j, uint32_t now);
  void sendSdes();
  void sendInfo();
  void setQso(Qso q);
  void startOutgoing(uint32_t ip, const std::string& call, uint32_t now);

  Net& net_;
  frn::Listener& li_;
  Config cfg_;
  bool running_ = false;
  uint32_t now_ = 0;

  // directory
  bool registered_ = false;
  bool regBusy_ = false;          // last status sent
  uint32_t nextRegister_ = 0;
  bool netWasReady_ = false;     // re-register after a proxy (re)connection
  DirJob job_ = DJ_NONE;
  DirStep step_ = DS_CONNECTING;
  uint32_t jobSince_ = 0;
  uint32_t dirIp_ = 0;
  uint32_t dirIpAt_ = 0;
  std::string dirLine_;
  std::string dirReply_;
  int listState_ = 0;             // 0 wait @@@, 1 count, 2 call, 3 data, 4 id, 5 ip, 6 end
  long listCount_ = 0;
  std::string entCall_;
  uint32_t entId_ = 0;
  uint32_t lookupId_ = 0;
  uint32_t scanned_ = 0;          // stations read in the current lookup
  size_t dirBytes_ = 0;           // bytes received in the current directory job

  // QSO
  Qso qso_ = QSO_IDLE;
  uint32_t rip_ = 0;
  std::string rcall_, rname_;
  uint32_t lastSdesTx_ = 0, lastSdesRx_ = 0, lastAudio_ = 0, lastRxAudio_ = 0;
  int retries_ = 0;
  uint16_t seq_ = 0;
  bool rxActive_ = false;
  void* enc_ = nullptr;
  void* dec_ = nullptr;
  int16_t pcm_[PACKET_SAMPLES];
};

}  // namespace el
