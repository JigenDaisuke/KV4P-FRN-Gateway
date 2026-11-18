#include "frn_client.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace frn {

namespace {
enum Cmd : uint8_t {
  DT_IDLE = 0,
  DT_DO_TX = 1,
  DT_VOICE_BUFFER = 2,
  DT_CLIENT_LIST = 3,
  DT_TEXT_MESSAGE = 4,
  DT_NET_NAMES = 5,
  DT_ADMIN_LIST = 6,
  DT_ACCESS_LIST = 7,
  DT_BLOCK_LIST = 8,
  DT_MUTE_LIST = 9,
  DT_ACCESS_MODE = 10,
};
constexpr size_t CLIENT_INDEX_SIZE = 2;

inline bool elapsed(uint32_t now, uint32_t since, uint32_t ms) { return (uint32_t)(now - since) >= ms; }
}  // namespace

const char* stateName(State s) {
  switch (s) {
    case State::Disconnected: return "DISCONNECTED";
    case State::Connecting: return "CONNECTING";
    case State::LoggingIn1: return "LOGGING_IN_1";
    case State::LoggingIn2: return "LOGGING_IN_2";
    case State::Idle: return "IDLE";
    case State::TxWaiting: return "TX_WAITING";
    case State::TxApproved: return "TX_APPROVED";
    case State::TxAudio: return "TX_AUDIO";
    case State::RxAudio: return "RX_AUDIO";
    case State::RxClientListHeader: return "RX_CLIENT_LIST_HEADER";
    case State::RxClientList: return "RX_CLIENT_LIST";
    case State::RxList: return "RX_LIST";
    case State::LoginRejected: return "LOGIN_REJECTED";
  }
  return "?";
}

bool Client::loggedIn() const {
  switch (state_) {
    case State::Disconnected:
    case State::Connecting:
    case State::LoggingIn1:
    case State::LoggingIn2:
    case State::LoginRejected:
      return false;
    default:
      return true;
  }
}

// state_ is the parser state; the TX state lives in tx_ and is reported
// on top of an idle parser.
State Client::state() const {
  if (state_ == State::Idle) {
    if (tx_ == Tx::Granted) return State::TxAudio;
    if (tx_ == Tx::Waiting) return State::TxWaiting;
  }
  return state_;
}

void Client::log(const char* fmt, ...) {
  char buf[256];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  li_.onFrnLog(buf);
}

void Client::setState(State s) {
  if (s == state_) return;
  State before = state();
  state_ = s;
  stateSince_ = now_;
  if (state() != before) li_.onFrnState(state());
}

void Client::setTx(Tx t) {
  if (t == tx_) return;
  State before = state();
  tx_ = t;
  if (state() != before) li_.onFrnState(state());
}

std::string Client::clean(const std::string& v) {
  // Values travel inside <XX>..</XX>: characters that would break the line
  // or the tags make AlterFRN drop the connection (InvalidConnectString).
  std::string o;
  o.reserve(v.size());
  for (char c : v) {
    if (c == '<') o += '(';
    else if (c == '>') o += ')';
    else if (c == '\r' || c == '\n') o += ' ';
    else o += c;
  }
  size_t a = o.find_first_not_of(' ');
  if (a == std::string::npos) return std::string();
  size_t b = o.find_last_not_of(' ');
  return o.substr(a, b - a + 1);
}

void Client::start(uint32_t nowMs) {
  now_ = nowMs;
  running_ = true;
  reconnectDelay_ = RECONNECT_MIN_MS;
  useBackup_ = false;
  tr_.close();
  setTx(Tx::None);
  setState(State::Disconnected);
  reconnectAt_ = nowMs;  // connect on next loop
}

void Client::stop() {
  running_ = false;
  wantTx_ = false;
  if (receivingVoice_) {
    receivingVoice_ = false;
    li_.onFrnVoiceEnd();
  }
  tr_.close();
  inLen_ = 0;
  setTx(Tx::None);
  setState(State::Disconnected);
}

void Client::dropAndRetry(uint32_t nowMs, uint32_t delayMs) {
  tr_.close();
  inLen_ = 0;
  if (receivingVoice_) {
    receivingVoice_ = false;
    li_.onFrnVoiceEnd();
  }
  wantTx_ = false;
  setTx(Tx::None);
  if (state_ != State::LoginRejected) setState(State::Disconnected);
  reconnectAt_ = nowMs + delayMs;
  log("reconnect in %u s", (unsigned)(delayMs / 1000));
}

void Client::doConnect(uint32_t nowMs) {
  // Backup: configured one first, otherwise the one announced by the server.
  std::string bk = cfg_.backupServer;
  uint16_t bkPort = cfg_.backupPort;
  if (bk.empty()) {
    bk = learnedBackup_;
    bkPort = learnedBackupPort_;
  }
  const bool haveBackup = !bk.empty();
  const char* host = (useBackup_ && haveBackup) ? bk.c_str() : cfg_.server.c_str();
  uint16_t port = (useBackup_ && haveBackup && bkPort) ? bkPort : cfg_.port;
  setState(State::Connecting);
  log("connecting to %s:%u", host, (unsigned)port);
  stats_.reconnects++;
  if (!tr_.connect(host, port)) {
    log("connect failed");
    if (haveBackup) useBackup_ = !useBackup_;
    uint32_t d = reconnectDelay_;
    reconnectDelay_ = (uint32_t)(reconnectDelay_ * 1.2f);
    if (reconnectDelay_ > RECONNECT_MAX_MS) reconnectDelay_ = RECONNECT_MAX_MS;
    dropAndRetry(now_, d);
    return;
  }
  inLen_ = 0;
  lastRx_ = lastCmd_ = lastPing_ = nowMs;
  lastTalkerIdx_ = -1;
  setState(State::LoggingIn1);
  sendLogin();
}

void Client::sendLogin() {
  std::string s;
  s.reserve(512);
  s += "CT:";
  s += "<VX>" + clean(cfg_.version) + "</VX>";
  s += "<EA>" + clean(cfg_.email) + "</EA>";
  s += "<PW>" + clean(cfg_.password) + "</PW>";
  s += "<ON>" + clean(cfg_.callsignAndUser) + "</ON>";
  s += "<CL>" + clean(cfg_.clientType) + "</CL>";
  s += "<BC>" + clean(cfg_.bandAndChannel) + "</BC>";
  s += "<DS>" + clean(cfg_.description) + "</DS>";
  s += "<NN>" + clean(cfg_.country) + "</NN>";
  s += "<CT>" + clean(cfg_.city) + "</CT>";
  s += "<NT>" + clean(cfg_.net) + "</NT>";
  s += "\r\n";
  log("login as '%s' net '%s'", cfg_.callsignAndUser.c_str(), cfg_.net.c_str());
  writeAll((const uint8_t*)s.data(), s.size());
}

bool Client::writeAll(const uint8_t* p, size_t n) {
  size_t done = 0;
  int stalls = 0;
  while (done < n) {
    size_t w = tr_.write(p + done, n - done);
    if (w == 0) {
      if (++stalls > 3) {
        log("write failed");
        dropAndRetry(now_, RECONNECT_MIN_MS);
        return false;
      }
      continue;
    }
    done += w;
  }
  return true;
}

void Client::sendRequest(const char* rq) {
  if (!tr_.connected()) return;
  char buf[16];
  int n = snprintf(buf, sizeof(buf), "%s\r\n", rq);
  writeAll((const uint8_t*)buf, (size_t)n);
}

void Client::requestTx(uint32_t nowMs) {
  now_ = nowMs;
  if (!loggedIn()) return;
  wantTx_ = true;
  if (tx_ == Tx::None) {
    sendRequest("TX0");
    lastTx0_ = nowMs;
    setTx(Tx::Waiting);
  }
}

bool Client::sendVoice(const int16_t* pcm1600, uint32_t nowMs) {
  now_ = nowMs;
  if (!txActive()) return false;
  codec_.encodePacket(pcm1600, gsmBuf_);
  // Header and payload in one write, like the reference client.
  uint8_t pkt[5 + PACKET_BYTES];
  memcpy(pkt, "TX1\r\n", 5);
  memcpy(pkt + 5, gsmBuf_, PACKET_BYTES);
  if (!writeAll(pkt, sizeof(pkt))) return false;
  stats_.voiceTx++;
  return true;
}

void Client::endTx(uint32_t nowMs) {
  now_ = nowMs;
  bool was = wantTx_ || tx_ != Tx::None;
  wantTx_ = false;
  setTx(Tx::None);
  // Verified on AlterFRN: RX0 also cancels a request not yet granted.
  if (was && loggedIn()) sendRequest("RX0");
}

void Client::loop(uint32_t nowMs) {
  now_ = nowMs;
  if (!running_) return;

  if (state_ == State::Disconnected || state_ == State::LoginRejected) {
    if ((int32_t)(nowMs - reconnectAt_) >= 0) {
      if (state_ == State::LoginRejected) setState(State::Disconnected);
      doConnect(nowMs);
    }
    return;
  }

  if (!tr_.connected()) {
    log("connection lost");
    dropAndRetry(nowMs, RECONNECT_MIN_MS);
    return;
  }

  // Read whatever is available.
  for (int i = 0; i < 8 && inLen_ < IN_BUF; i++) {
    int r = tr_.read(in_ + inLen_, IN_BUF - inLen_);
    if (r <= 0) break;
    inLen_ += (size_t)r;
    lastRx_ = nowMs;
  }
  if (inLen_ > 0) {
    size_t used = process(in_, inLen_, nowMs);
    if (state_ == State::Disconnected || state_ == State::LoginRejected) return;  // dropped inside
    if (used > 0) {
      memmove(in_, in_ + used, inLen_ - used);
      inLen_ -= used;
    }
  }

  // Timers
  if (!loggedIn()) {
    if (elapsed(nowMs, stateSince_, LOGIN_TIMEOUT_MS)) {
      log("login timeout");
      dropAndRetry(nowMs, RECONNECT_MIN_MS);
    }
    return;
  }
  // The server may send nothing back to the station holding the floor for
  // the whole over: never time out while we transmit.
  if (tx_ != Tx::Granted && elapsed(nowMs, lastRx_, CON_TIMEOUT_MS)) {
    log("connection timeout (no data for %u s)", (unsigned)(CON_TIMEOUT_MS / 1000));
    dropAndRetry(nowMs, RECONNECT_MIN_MS);
    return;
  }
  // Keepalive always, also while transmitting.
  if (elapsed(nowMs, lastPing_, KEEPALIVE_MS)) {
    sendRequest("P");
    lastPing_ = nowMs;
  }
  if (receivingVoice_ && elapsed(nowMs, lastVoice_, RX_TIMEOUT_MS)) {
    receivingVoice_ = false;
    lastTalkerIdx_ = -1;
    li_.onFrnVoiceEnd();
  }
  // Floor not granted yet (net busy) and the local station still talks: ask again.
  if (wantTx_ && tx_ == Tx::Waiting && !receivingVoice_ && elapsed(nowMs, lastTx0_, TX0_RETRY_MS)) {
    sendRequest("TX0");
    lastTx0_ = nowMs;
  }
}

size_t Client::handleLine(const uint8_t* p, size_t n, std::string& line, bool force) {
  const void* nl = memchr(p, '\n', n);
  size_t len, used;
  if (nl) {
    len = (const uint8_t*)nl - p;
    used = len + 1;
  } else if (force) {
    len = used = n;  // over-long line: take what we have
  } else {
    return 0;
  }
  size_t start = 0;
  while (start < len && p[start] == '\r') start++;
  while (len > start && p[len - 1] == '\r') len--;
  line.assign((const char*)p + start, len - start);
  return used;
}

size_t Client::process(const uint8_t* p, size_t n, uint32_t nowMs) {
  size_t total = 0;
  while (total < n) {
    const uint8_t* q = p + total;
    size_t avail = n - total;
    size_t used = 0;
    const bool full = (n >= IN_BUF);
    std::string line;

    switch (state_) {
      case State::LoggingIn1:
        used = handleLine(q, avail, line, full);
        if (used) {
          serverInfo_ = line;  // protocol version, e.g. 2014003
          log("server protocol '%s'", line.c_str());
          setState(State::LoggingIn2);
        }
        break;

      case State::LoggingIn2:
        used = handleLine(q, avail, line, full);
        if (used) {
          serverInfo_ = line;
          // Legacy challenge (pre-2014 servers; AlterFRN still knows it).
          std::string kp = extractTag(line, "KP");
          if (kp.size() == 6 && kp.find_first_not_of("0123456789") == std::string::npos) {
            int a = atoi(kp.substr(0, 2).c_str()), b = atoi(kp.substr(2, 2).c_str()), c = atoi(kp.substr(4, 2).c_str());
            char x[16];
            snprintf(x, sizeof(x), "%05d", (a + 2) * (b + 1) + (c + 4) * (c + 7));
            char ans[6] = {x[3], x[0], x[2], x[4], x[1], 0};
            sendRequest(ans);
            log("answered KP challenge");
          }
          std::string bn = extractTag(line, "BN"), bp = extractTag(line, "BP");
          if (!bn.empty() && !bp.empty() && bp.find_first_not_of("0123456789") == std::string::npos) {
            learnedBackup_ = bn;
            learnedBackupPort_ = (uint16_t)atoi(bp.c_str());
          }
          std::string al = extractTag(line, "AL");
          if (al != "OK" && al != "OWNER" && al != "NETOWNER" && al != "ADMIN") {
            log("login REJECTED (AL=%s): '%s'", al.empty() ? "?" : al.c_str(), line.c_str());
            setState(State::LoginRejected);
            dropAndRetry(nowMs, REJECTED_RETRY_MS);
            return n;
          }
          log("login ok (%s)", al.c_str());
          stats_.logins++;
          reconnectDelay_ = RECONNECT_MIN_MS;
          useBackup_ = false;
          lastCmd_ = lastPing_ = nowMs;
          setTx(Tx::None);
          setState(State::Idle);
          sendRequest("RX0");
        }
        break;

      case State::TxApproved:
        if (avail >= CLIENT_INDEX_SIZE) {
          used = CLIENT_INDEX_SIZE;
          setState(State::Idle);
          if (wantTx_ && tx_ == Tx::Waiting) {
            setTx(Tx::Granted);
            li_.onFrnTxApproved();
          }
          // A grant we no longer want is ignored: RX0 was already sent by endTx().
        }
        break;

      case State::Idle:
        used = handleCommand(q, avail, nowMs);
        break;

      case State::RxAudio:
        used = handleVoice(q, avail, nowMs);
        break;

      case State::RxClientListHeader:
        if (avail >= CLIENT_INDEX_SIZE) {
          used = CLIENT_INDEX_SIZE;
          linesToRead_ = -1;
          listItems_.clear();
          setState(State::RxClientList);
        }
        break;

      case State::RxList:
      case State::RxClientList:
        used = handleLine(q, avail, line, full);
        if (used) onListLine(line);
        break;

      default:
        return n;  // not connected: discard
    }

    if (used == 0) break;
    total += used;
    if (state_ == State::Disconnected) return n;
  }
  return total;
}

size_t Client::handleCommand(const uint8_t* p, size_t n, uint32_t nowMs) {
  if (n < 1) return 0;
  const uint8_t cmd = p[0];
  lastCmd_ = nowMs;
  switch (cmd) {
    case DT_IDLE:
      // Just an idle marker (e.g. answer to P). It does NOT deny or end a TX.
      break;
    case DT_DO_TX:
      setState(State::TxApproved);
      break;
    case DT_VOICE_BUFFER:
      setState(State::RxAudio);
      break;
    case DT_CLIENT_LIST:
      setState(State::RxClientListHeader);
      break;
    case DT_TEXT_MESSAGE:
    case DT_NET_NAMES:
    case DT_ADMIN_LIST:
    case DT_ACCESS_LIST:
    case DT_BLOCK_LIST:
    case DT_MUTE_LIST:
    case DT_ACCESS_MODE:
      listCode_ = cmd;
      linesToRead_ = -1;
      listItems_.clear();
      setState(State::RxList);
      break;
    default:
      log("unknown command byte %u", (unsigned)cmd);  // skipped, never stall
      break;
  }
  return 1;
}

size_t Client::handleVoice(const uint8_t* p, size_t n, uint32_t nowMs) {
  if (n < CLIENT_INDEX_SIZE + PACKET_BYTES) return 0;
  const int idx = ((int)p[0] << 8) | p[1];
  if (!receivingVoice_ || idx != lastTalkerIdx_) {
    receivingVoice_ = true;
    lastTalkerIdx_ = idx;
    codec_.resetDecoder();  // fresh codec state per talker
    lastTalker_ = (idx > 0 && (size_t)idx <= clients_.size()) ? clients_[idx - 1] : std::string("?");
    log("voice from %s", lastTalker_.c_str());
    li_.onFrnVoiceStart(lastTalker_.c_str());
  }
  if (!codec_.decodePacket(p + CLIENT_INDEX_SIZE, pcmBuf_)) stats_.badFrames++;
  stats_.voiceRx++;
  li_.onFrnVoice(pcmBuf_, PACKET_SAMPLES);
  setState(State::Idle);
  lastVoice_ = nowMs;
  sendRequest("P");
  lastPing_ = nowMs;
  return CLIENT_INDEX_SIZE + PACKET_BYTES;
}

void Client::onListLine(const std::string& line) {
  if (linesToRead_ < 0) {
    long v = strtol(line.c_str(), nullptr, 10);
    if (v < 0) v = 0;
    if (v > 10000) v = 10000;
    linesToRead_ = (int)v;
  } else {
    if (listItems_.size() < MAX_CLIENTS) listItems_.push_back(line);
    linesToRead_--;
  }
  if (linesToRead_ == 0) finishList();
}

void Client::finishList() {
  if (state_ == State::RxClientList) {
    clients_.clear();
    for (auto& it : listItems_) {
      std::string on = extractTag(it, "ON");
      if (on.empty()) on = it;
      if (on.size() > 60) on.resize(60);
      clients_.push_back(on);
    }
    log("client list: %u stations", (unsigned)clients_.size());
  } else if (listCode_ == DT_TEXT_MESSAGE && !listItems_.empty()) {
    // lines: sender id, text..., kind ("A" = room, "P" = private)
    std::string sender = listItems_[0];
    std::string text;
    size_t last = listItems_.size() >= 3 ? listItems_.size() - 1 : listItems_.size();
    for (size_t i = 1; i < last; i++) {
      if (!text.empty()) text += ' ';
      text += listItems_[i];
    }
    std::string msg = (sender.empty() ? std::string("server") : sender) + ": " + text;
    li_.onFrnText(msg.c_str());
  }
  listItems_.clear();
  linesToRead_ = -1;
  setState(State::Idle);
}

std::string Client::extractTag(const std::string& s, const char* tag) {
  std::string open = std::string("<") + tag + ">";
  size_t a = s.find(open);
  if (a == std::string::npos) return std::string();
  a += open.size();
  std::string close = std::string("</") + tag + ">";
  size_t b = s.find(close, a);
  if (b == std::string::npos) b = s.find('<', a);  // tolerate a missing closing tag
  if (b == std::string::npos) b = s.size();
  return s.substr(a, b - a);
}

}  // namespace frn
