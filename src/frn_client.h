// Free Radio Network (FRN) client protocol, gateway-capable.
//
// Protocol behaviour follows the client verified against a real AlterFRN
// r7348 server (JigenDaisuke/FreeRadioNetwork, client-pc/frnclient/protocol.py),
// cross-checked with SvxLink ModuleFrn. Where the two differ, the AlterFRN-
// verified behaviour wins: periodic "P" keepalive (also while talking),
// DT_IDLE does not touch the TX state, <AL> must be OK/OWNER/NETOWNER/ADMIN,
// legacy <KP> challenge answered, backup server taken from <BN>/<BP>.
//
// Wire summary
//   client -> server : "CT:<VX>..</VX><EA>..</EA><PW>..</PW><ON>..</ON><CL>..</CL>
//                       <BC>..</BC><DS>..</DS><NN>..</NN><CT>..</CT><NT>..</NT>\n"
//                      then requests "RX0" "TX0" "TX1" "P", each + "\r\n".
//                      "TX1\r\n" is followed by 325 bytes of WAV49 GSM.
//   server -> client : two login lines (version, then <AL>..</AL> result),
//                      then 1-byte command codes:
//                        0 IDLE, 1 DO_TX (+2 byte idx), 2 VOICE (+2 byte idx +325),
//                        3 CLIENT_LIST (+2 byte hdr + count line + N lines),
//                        4..10 text lists (count line + N lines).
#pragma once
#include <stddef.h>
#include <stdint.h>

#include <string>
#include <vector>

#include "frn_codec.h"

namespace frn {

struct Config {
  std::string server = "server.voip-italia.net";
  uint16_t port = 10024;
  std::string backupServer;      // empty = same as server
  uint16_t backupPort = 0;       // 0 = same as port
  std::string version = "2014003";
  std::string email;
  std::string password;          // FRN "dynamic password" received at registration
  std::string callsignAndUser;   // e.g. "IZ0ABC, Mario"
  std::string clientType = "1";  // 0 = crosslink, 1 = gateway, 2 = PC only
  std::string bandAndChannel;    // e.g. "145.43750FM CTC88.5"
  std::string description = "KV4P ESP32 FRN Gateway";
  std::string country = "Italy";
  std::string city;              // "City - Area"
  std::string net = "Nazionale";
};

enum class State : uint8_t {
  Disconnected,
  Connecting,
  LoggingIn1,
  LoggingIn2,
  Idle,
  TxWaiting,   // reported only (TX request pending)
  TxApproved,  // parser: reading the 2-byte header after DO_TX
  TxAudio,     // reported only (floor granted)
  RxAudio,
  RxClientListHeader,
  RxClientList,
  RxList,
  LoginRejected,
};
const char* stateName(State s);

// Byte transport (TCP). Implemented with WiFiClient on ESP32 and POSIX
// sockets in the PC test harness.
class Transport {
 public:
  virtual ~Transport() {}
  virtual bool connect(const char* host, uint16_t port) = 0;  // blocking, short timeout
  virtual void close() = 0;
  virtual bool connected() = 0;
  virtual int read(uint8_t* buf, size_t maxLen) = 0;           // non-blocking; 0 if none
  virtual size_t write(const uint8_t* buf, size_t len) = 0;
};

class Listener {
 public:
  virtual ~Listener() {}
  virtual void onFrnState(State) {}
  virtual void onFrnLog(const char* msg) { (void)msg; }
  // A remote station started talking. `who` = its <ON> field if known.
  virtual void onFrnVoiceStart(const char* who) { (void)who; }
  // 200 ms of decoded audio, 1600 samples @ 8 kHz.
  virtual void onFrnVoice(const int16_t* pcm, size_t samples) { (void)pcm; (void)samples; }
  // No voice packet for RX_TIMEOUT ms: the remote over is finished.
  virtual void onFrnVoiceEnd() {}
  // Server granted our TX request.
  virtual void onFrnTxApproved() {}
  virtual void onFrnText(const char* line) { (void)line; }
};

class Client {
 public:
  static constexpr uint32_t CON_TIMEOUT_MS = 20000;   // not applied while we hold the floor
  static constexpr uint32_t LOGIN_TIMEOUT_MS = 15000;
  static constexpr uint32_t RX_TIMEOUT_MS = 1000;
  static constexpr uint32_t KEEPALIVE_MS = 1000;      // AlterFRN drops idle clients after ~12 s
  static constexpr uint32_t RECONNECT_MIN_MS = 5000;
  static constexpr uint32_t RECONNECT_MAX_MS = 120000;
  static constexpr uint32_t REJECTED_RETRY_MS = 300000;  // wrong password / blocked
  static constexpr uint32_t TX0_RETRY_MS = 2000;
  static constexpr size_t IN_BUF = 4096;
  static constexpr size_t MAX_CLIENTS = 100;

  Client(Transport& t, Listener& l) : tr_(t), li_(l) {}

  void setConfig(const Config& c) { cfg_ = c; }
  const Config& config() const { return cfg_; }

  // Start (or restart) connecting. Call from the network task.
  void start(uint32_t nowMs);
  void stop();

  // Drive the state machine. Call often (every few ms).
  void loop(uint32_t nowMs);

  // --- Gateway TX side (radio -> network) ---
  // Local squelch opened: ask the server for the floor.
  void requestTx(uint32_t nowMs);
  // True once the server granted the floor and voice can be sent.
  bool txActive() const { return tx_ == Tx::Granted && loggedIn(); }
  bool txWanted() const { return wantTx_; }
  // Send 200 ms (1600 samples @ 8 kHz). Returns false if not in TX.
  bool sendVoice(const int16_t* pcm1600, uint32_t nowMs);
  // Local squelch closed: release the floor.
  void endTx(uint32_t nowMs);

  State state() const;
  bool loggedIn() const;
  const std::string& lastTalker() const { return lastTalker_; }
  const std::vector<std::string>& clients() const { return clients_; }
  const std::string& serverInfo() const { return serverInfo_; }

  struct Stats {
    uint32_t voiceRx = 0, voiceTx = 0, badFrames = 0, reconnects = 0, logins = 0;
  };
  const Stats& stats() const { return stats_; }

 private:
  enum class Tx : uint8_t { None, Waiting, Granted };
  void setState(State s);
  void setTx(Tx t);
  static std::string clean(const std::string& v);
  void log(const char* fmt, ...);
  void doConnect(uint32_t nowMs);
  void dropAndRetry(uint32_t nowMs, uint32_t delayMs);
  void sendLogin();
  void sendRequest(const char* rq);
  bool writeAll(const uint8_t* p, size_t n);
  // Parsers: return bytes consumed (0 = need more data).
  size_t process(const uint8_t* p, size_t n, uint32_t nowMs);
  size_t handleCommand(const uint8_t* p, size_t n, uint32_t nowMs);
  size_t handleVoice(const uint8_t* p, size_t n, uint32_t nowMs);
  size_t handleLine(const uint8_t* p, size_t n, std::string& line, bool force);
  void onListLine(const std::string& line);
  void finishList();
  static std::string extractTag(const std::string& s, const char* tag);

  Transport& tr_;
  Listener& li_;
  Config cfg_;
  Codec codec_;
  State state_ = State::Disconnected;
  bool running_ = false;
  bool useBackup_ = false;

  uint8_t in_[IN_BUF];
  size_t inLen_ = 0;

  uint32_t now_ = 0;
  uint32_t lastRx_ = 0;          // any data received
  uint32_t lastCmd_ = 0;         // last command byte received (keepalive)
  uint32_t lastVoice_ = 0;
  uint32_t stateSince_ = 0;
  uint32_t reconnectAt_ = 0;
  uint32_t reconnectDelay_ = RECONNECT_MIN_MS;
  uint32_t lastTx0_ = 0;
  uint32_t lastPing_ = 0;
  Tx tx_ = Tx::None;
  int lastTalkerIdx_ = -1;
  std::string learnedBackup_;
  uint16_t learnedBackupPort_ = 0;
  bool receivingVoice_ = false;
  bool wantTx_ = false;

  int linesToRead_ = -1;
  uint8_t listCode_ = 0;
  std::vector<std::string> listItems_;
  std::vector<std::string> clients_;
  std::string lastTalker_;
  std::string serverInfo_;
  Stats stats_;

  uint8_t gsmBuf_[PACKET_BYTES];
  int16_t pcmBuf_[PACKET_SAMPLES];
};

}  // namespace frn
