#include "echolink.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern "C" {
#include "gsm.h"
#include "md5.h"
}

namespace el {

namespace {
constexpr uint8_t RTCP_RR = 201, RTCP_SDES = 202, RTCP_BYE = 203;
constexpr uint8_t SDES_END = 0, SDES_CNAME = 1, SDES_NAME = 2, SDES_EMAIL = 3, SDES_PHONE = 4;
inline bool elapsed(uint32_t now, uint32_t since, uint32_t ms) { return (uint32_t)(now - since) >= ms; }
inline uint8_t* addText(uint8_t* p, const char* t) {
  size_t l = strlen(t);
  if (l > 255) l = 255;
  *p++ = (uint8_t)l;
  memcpy(p, t, l);
  return p + l;
}
}  // namespace

std::string ipToString(uint32_t ip) {
  char b[16];
  snprintf(b, sizeof(b), "%u.%u.%u.%u", (unsigned)(ip >> 24), (unsigned)((ip >> 16) & 255), (unsigned)((ip >> 8) & 255),
           (unsigned)(ip & 255));
  return b;
}

// ---------------------------------------------------------------- proxy ----
void ProxyNet::configure(const std::string& hosts, uint16_t port, const std::string& callsign,
                         const std::string& password) {
  hosts_.clear();
  std::string cur;
  for (char c : hosts + ",") {
    if (c == ',' || c == ';' || c == ' ' || c == '\t') {
      if (!cur.empty()) hosts_.push_back(cur);
      cur.clear();
    } else {
      cur += c;
    }
  }
  idx_ = 0;
  host_ = hosts_.empty() ? std::string() : hosts_[0];
  name_ = "proxy " + host_;
  port_ = port;
  call_ = callsign;
  pass_ = password;
  for (auto& c : call_) c = (char)toupper((unsigned char)c);
  for (auto& c : pass_) c = (char)toupper((unsigned char)c);  // like echolib
}

void ProxyNet::nextHost(uint32_t nowMs, const char* why) {
  char m[128];
  snprintf(m, sizeof(m), "proxy: %s %s", host_.c_str(), why);
  log(m);
  if (hosts_.size() > 1) {
    idx_ = (idx_ + 1) % hosts_.size();
    host_ = hosts_[idx_];
    name_ = "proxy " + host_;
    retryAt_ = nowMs + 2000;  // next proxy of the list almost at once
  }
}

void ProxyNet::open(uint32_t nowMs) {
  if (state_ != CLOSED || host_.empty() || (int32_t)(nowMs - retryAt_) < 0) return;
  retryAt_ = nowMs + 10000;  // RECONNECT_INTERVAL
  len_ = 0;
  dirState_ = 0;
  char m[96];
  snprintf(m, sizeof(m), "proxy: connecting to %s:%u", host_.c_str(), (unsigned)port_);
  log(m);
  if (tcp_.connect(host_.c_str(), port_)) state_ = WAIT_NONCE;
  else nextHost(nowMs, "not reachable");
}

void ProxyNet::close() {
  tcp_.close();
  state_ = CLOSED;
  len_ = 0;
  if (dirState_ && onDirClosed) {
    dirState_ = 0;
    onDirClosed();
  }
  dirState_ = 0;
}

bool ProxyNet::block(uint8_t type, uint32_t ip, const uint8_t* d, size_t n) {
  if (state_ != READY) return false;
  uint8_t h[9] = {type,
                  (uint8_t)(ip >> 24), (uint8_t)(ip >> 16), (uint8_t)(ip >> 8), (uint8_t)ip,
                  (uint8_t)n, (uint8_t)(n >> 8), (uint8_t)(n >> 16), (uint8_t)(n >> 24)};
  if (tcp_.write(h, 9) != 9) return false;
  if (n && tcp_.write(d, n) != n) return false;
  return true;
}

bool ProxyNet::dirOpen(uint32_t ip) {
  if (state_ != READY) return false;
  dirState_ = 1;
  return block(1, ip, nullptr, 0);
}

void ProxyNet::dirClose() {
  if (dirState_) block(3, 0, nullptr, 0);
  dirState_ = 0;
}

void ProxyNet::poll(uint32_t nowMs) {
  lastNow_ = nowMs;
  if (state_ == CLOSED) return;
  if (!tcp_.connected()) {
    bool early = state_ == WAIT_NONCE;
    close();
    if (early) nextHost(nowMs, "closed the connection at once (busy?)");
    else log("proxy: connection lost");
    return;
  }
  for (int i = 0; i < 8 && len_ < sizeof(buf_); i++) {
    int r = tcp_.read(buf_ + len_, sizeof(buf_) - len_);
    if (r <= 0) break;
    len_ += (size_t)r;
  }
  size_t used = 0;
  if (state_ == WAIT_NONCE) {
    if (len_ < 8) return;
    // CALL "\n" MD5(PASSWORD + nonce)
    uint8_t auth[64];
    size_t cl = call_.size() > 40 ? 40 : call_.size();
    memcpy(auth, call_.data(), cl);
    auth[cl] = '\n';
    std::string s = pass_;
    s.append((const char*)buf_, 8);
    md5_state_t st;
    md5_init(&st);
    md5_append(&st, (const md5_byte_t*)s.data(), (int)s.size());
    md5_finish(&st, auth + cl + 1);
    tcp_.write(auth, cl + 1 + 16);
    state_ = READY;
    log("proxy: authenticated");
    used = 8;
  }
  while (state_ == READY && len_ - used >= 9) {
    const uint8_t* p = buf_ + used;
    uint32_t ip = ((uint32_t)p[1] << 24) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 8) | p[4];
    uint32_t n = p[5] | ((uint32_t)p[6] << 8) | ((uint32_t)p[7] << 16) | ((uint32_t)p[8] << 24);
    if (n > sizeof(buf_) - 9) {
      log("proxy: oversized block, reconnecting");
      close();
      return;
    }
    if (len_ - used < 9 + n) break;
    handle(p[0], ip, p + 9, n);
    if (state_ != READY) return;
    used += 9 + n;
  }
  if (used) {
    memmove(buf_, buf_ + used, len_ - used);
    len_ -= used;
  }
}

void ProxyNet::handle(uint8_t type, uint32_t ip, const uint8_t* d, size_t n) {
  switch (type) {
    case 2:  // TCP_DATA
      if (onDir) onDir(d, n);
      break;
    case 3:  // TCP_CLOSE
      if (dirState_) {
        dirState_ = 0;
        if (onDirClosed) onDirClosed();
      }
      break;
    case 4: {  // TCP_STATUS
      uint32_t st = n == 4 ? (d[0] | ((uint32_t)d[1] << 8) | ((uint32_t)d[2] << 16) | ((uint32_t)d[3] << 24)) : 1;
      if (dirState_ == 1) {
        if (st == 0) {
          dirState_ = 2;
        } else {
          dirState_ = 0;
          log("proxy: directory connection refused");
          if (onDirClosed) onDirClosed();
        }
      }
      break;
    }
    case 5:  // UDP_DATA (audio port)
      if (onAudio) onAudio(ip, d, n);
      break;
    case 6:  // UDP_CONTROL
      if (onCtrl) onCtrl(ip, d, n);
      break;
    case 7:  // SYSTEM
      close();
      nextHost(lastNow_, n == 1 && d[0] == 1 ? "refused: BAD PASSWORD" : "refused: access denied");
      if (hosts_.size() <= 1) retryAt_ = lastNow_ + 60000;  // don't hammer it
      break;
    default:
      log("proxy: unknown block type");
      close();
      break;
  }
}

// --------------------------------------------------------------- client ----
Client::Client(Net& net, frn::Listener& li) : net_(net), li_(li) {
  enc_ = gsm_create();
  dec_ = gsm_create();
  net_.onCtrl = [this](uint32_t ip, const uint8_t* d, size_t n) { handleCtrl(ip, d, n); };
  net_.onAudio = [this](uint32_t ip, const uint8_t* d, size_t n) { handleAudio(ip, d, n); };
  net_.onDir = [this](const uint8_t* d, size_t n) { handleDir(d, n); };
  net_.onDirClosed = [this]() {
    if (job_ != DJ_NONE) {
      if (job_ == DJ_LOOKUP || !dirReply_.empty()) handleDir((const uint8_t*)"\n", 1);  // flush last line
      if (job_ != DJ_NONE) dirDone(false);
    }
  };
  net_.onLog = [this](const char* m) { log("%s", m); };
}

Client::~Client() {
  if (enc_) gsm_destroy((gsm)enc_);
  if (dec_) gsm_destroy((gsm)dec_);
}

void Client::setConfig(const Config& c) {
  cfg_ = c;
  for (auto& ch : cfg_.callsign) ch = (char)toupper((unsigned char)ch);
  if (cfg_.location.size() > 27) cfg_.location.resize(27);
}

void Client::log(const char* fmt, ...) {
  char b[200];
  int n = snprintf(b, sizeof(b), "EL: ");
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(b + n, sizeof(b) - n, fmt, ap);
  va_end(ap);
  li_.onFrnLog(b);
}

const char* Client::stateName() const {
  if (!running_) return "OFF";
  if (!net_.ready()) return "NET_WAIT";
  if (qso_ == QSO_CONNECTED) return "CONNECTED";
  if (qso_ == QSO_CONNECTING || lookupId_) return "CONNECTING";
  return registered_ ? "ONLINE" : "REGISTERING";
}

void Client::start(uint32_t nowMs) {
  now_ = nowMs;
  running_ = true;
  registered_ = false;
  nextRegister_ = nowMs;
  job_ = DJ_NONE;
  lookupId_ = 0;
  setQso(QSO_IDLE);
  net_.open(nowMs);
}

void Client::stop(uint32_t nowMs) {
  now_ = nowMs;
  if (qso_ != QSO_IDLE) disconnect(nowMs, "system change");
  if (job_ != DJ_NONE) net_.dirClose();
  job_ = DJ_NONE;
  lookupId_ = 0;
  net_.close();
  running_ = false;
  registered_ = false;
}

void Client::setQso(Qso q) {
  if (q == qso_) return;
  bool wasConn = qso_ == QSO_CONNECTED;
  qso_ = q;
  if (q == QSO_IDLE && rxActive_) {
    rxActive_ = false;
    li_.onFrnVoiceEnd();
  }
  if ((q == QSO_CONNECTED) != wasConn) {
    nextRegister_ = now_ + 1000;  // refresh ONLINE/BUSY status soon
    if (onQso) onQso(q == QSO_CONNECTED, rcall_);
  }
}

// ---------------------------------------------------------------- packets --
size_t Client::makeSdes(uint8_t* p0, const std::string& call, const std::string& name) {
  uint8_t* p = p0;
  // null receiver report first (RTP version field 3 = 0xC0, as EchoLink does)
  *p++ = 3 << 6;
  *p++ = RTCP_RR;
  *p++ = 0;
  *p++ = 1;
  *p++ = 0; *p++ = 0; *p++ = 0; *p++ = 0;
  uint8_t* s = p;
  uint16_t ver = (3 << 14) | RTCP_SDES | (1 << 8);
  s[0] = ver >> 8;
  s[1] = ver & 0xff;
  s[4] = s[5] = s[6] = s[7] = 0;
  uint8_t* ap = s + 8;
  char tmp[256];
  *ap++ = SDES_CNAME;
  ap = addText(ap, "CALLSIGN");
  *ap++ = SDES_NAME;
  snprintf(tmp, sizeof(tmp), "%-15s%s", call.c_str(), name.c_str());
  ap = addText(ap, tmp);
  *ap++ = SDES_EMAIL;
  ap = addText(ap, "CALLSIGN");
  *ap++ = SDES_PHONE;
  ap = addText(ap, "08:30");
  *ap++ = SDES_END;
  *ap++ = 0;
  while ((ap - s) & 3) *ap++ = 0;
  size_t words = (size_t)((ap - s) / 4) - 1;
  s[2] = (uint8_t)(words >> 8);
  s[3] = (uint8_t)words;
  return (size_t)(ap - p0);
}

size_t Client::makeBye(uint8_t* p0) {
  uint8_t* p = p0;
  *p++ = 3 << 6;
  *p++ = RTCP_RR;
  *p++ = 0;
  *p++ = 1;
  *p++ = 0; *p++ = 0; *p++ = 0; *p++ = 0;
  uint8_t* s = p;
  uint16_t ver = (3 << 14) | RTCP_BYE | (1 << 8);
  s[0] = ver >> 8;
  s[1] = ver & 0xff;
  s[4] = s[5] = s[6] = s[7] = 0;
  uint8_t* ap = addText(s + 8, "jan2002");
  while ((ap - s) & 3) *ap++ = 0;
  size_t words = (size_t)((ap - s) / 4) - 1;
  s[2] = (uint8_t)(words >> 8);
  s[3] = (uint8_t)words;
  return (size_t)(ap - p0);
}

static bool rtcpHas(const uint8_t* p, size_t n, uint8_t type) {
  const uint8_t* end = p + n;
  if (n < 4) return false;
  int v = (p[0] >> 6) & 3;
  if ((v != 3 && v != 1) || (p[0] & 0x20) || (p[1] != 200 && p[1] != RTCP_RR)) return false;
  while (p + 4 <= end && (((p[0] >> 6) & 3) == 3 || ((p[0] >> 6) & 3) == 1)) {
    if (p[1] == type) return true;
    size_t len = (((size_t)p[2] << 8) | p[3]) * 4 + 4;
    if (len == 0) break;
    p += len;
  }
  return false;
}

bool Client::isSdes(const uint8_t* p, size_t n) { return rtcpHas(p, n, RTCP_SDES); }
bool Client::isBye(const uint8_t* p, size_t n) { return rtcpHas(p, n, RTCP_BYE); }

bool Client::sdesName(const uint8_t* p, size_t n, std::string& out) {
  const uint8_t* end = p + n;
  while (p + 4 <= end) {
    size_t len = (((size_t)p[2] << 8) | p[3]) * 4 + 4;
    if (p + len > end) break;
    if (p[1] == RTCP_SDES && (p[0] & 0x1f) > 0) {
      const uint8_t* cp = p + 8;
      const uint8_t* lp = p + len;
      while (cp + 2 <= lp) {
        uint8_t t = cp[0], l = cp[1];
        if (t == SDES_END || cp + 2 + l > lp) break;
        if (t == SDES_NAME) {
          out.assign((const char*)cp + 2, l);
          return true;
        }
        cp += 2 + l;
      }
      return false;
    }
    p += len;
  }
  return false;
}

static void splitCallName(const std::string& id, std::string& call, std::string& name) {
  size_t sp = id.find_first_of(" \t\r\n");
  call = id.substr(0, sp);
  name.clear();
  if (sp != std::string::npos) {
    size_t a = id.find_first_not_of(" \t\r\n", sp);
    if (a != std::string::npos) name = id.substr(a);
  }
  std::string c;  // sanitise: A-Z 0-9 - * / only, max 32
  for (char ch : call) {
    ch = (char)toupper((unsigned char)ch);
    if (isalnum((unsigned char)ch) || ch == '-' || ch == '*' || ch == '/') c += ch;
    if (c.size() >= 32) break;
  }
  call = c;
}

void Client::sendSdes() {
  uint8_t p[300];
  size_t n = makeSdes(p, cfg_.callsign, cfg_.sysopName);
  net_.sendCtrl(rip_, p, n);
  lastSdesTx_ = now_;
}

void Client::sendInfo() {
  std::string m = "oNDATA\r" + cfg_.info;
  for (auto& c : m)
    if (c == '\n') c = '\r';
  net_.sendAudio(rip_, (const uint8_t*)m.c_str(), m.size() + 1);  // with the trailing NUL
}

// ------------------------------------------------------------------ input --
void Client::handleCtrl(uint32_t ip, const uint8_t* d, size_t n) {
  if (!running_) return;
  if (dbgPackets_ > 0) {
    dbgPackets_--;
    log("ctrl packet from %s: %u bytes, %02x %02x %s%s", ipToString(ip).c_str(), (unsigned)n, n > 0 ? d[0] : 0,
        n > 1 ? d[1] : 0, isSdes(d, n) ? "SDES" : "", isBye(d, n) ? "BYE" : "");
  }
  if (qso_ != QSO_IDLE && ip == rip_) {
    if (isBye(d, n)) {
      log("%s hung up", rcall_.c_str());
      setQso(QSO_IDLE);
      return;
    }
    if (isSdes(d, n)) {
      std::string id;
      if (sdesName(d, n, id)) {
        std::string c, nm;
        splitCallName(id, c, nm);
        if (!c.empty()) rcall_ = c;
        if (!nm.empty()) rname_ = nm;
      }
      lastSdesRx_ = now_;
      if (qso_ == QSO_CONNECTING) {
        log("connected to %s", rcall_.c_str());
        lastAudio_ = now_;
        setQso(QSO_CONNECTED);
        sendInfo();
        if (onConnectResult) onConnectResult(true, rcall_);
      }
    }
    return;
  }
  // A packet from a station we are not talking to: maybe an incoming call.
  if (!isSdes(d, n)) return;
  std::string id, c, nm;
  if (!sdesName(d, n, id)) return;
  splitCallName(id, c, nm);
  if (c.empty()) return;
  if (qso_ == QSO_IDLE && lookupId_ == 0 && cfg_.acceptIncoming) {
    rip_ = ip;
    rcall_ = c;
    rname_ = nm;
    lastSdesRx_ = lastAudio_ = now_;
    log("incoming connection from %s (%s) %s", c.c_str(), nm.c_str(), ipToString(ip).c_str());
    sendSdes();
    setQso(QSO_CONNECTED);
    sendInfo();
  } else {
    uint8_t b[64];
    size_t bn = makeBye(b);
    net_.sendCtrl(ip, b, bn);
    log("rejected %s (%s)", c.c_str(), cfg_.acceptIncoming ? "busy" : "incoming disabled");
  }
}

void Client::handleAudio(uint32_t ip, const uint8_t* d, size_t n) {
  if (dbgPackets_ > 0 && qso_ != QSO_CONNECTED) {
    dbgPackets_--;
    log("audio-port packet from %s: %u bytes (not connected yet)", ipToString(ip).c_str(), (unsigned)n);
  }
  if (!running_ || qso_ != QSO_CONNECTED || ip != rip_ || n < 7) return;
  if (d[0] != 0xC0) {
    if (memcmp(d + 1, "NDATA", 5) == 0) {
      const char* t = (const char*)d + 6;
      size_t l = strnlen(t, n - 6);
      std::string s(t, l > 100 ? 100 : l);
      for (auto& ch : s)
        if (ch == '\r' || ch == '\n') ch = ' ';
      log("%s %s", d[6] == '\r' ? "info:" : "chat:", s.c_str());
    }
    return;
  }
  if (n < (size_t)(RTP_HEADER + FRAMES_PER_PACKET * GSM_FRAME_BYTES) || d[1] != 0x03) return;  // GSM only
  for (int f = 0; f < FRAMES_PER_PACKET; f++) {
    gsm_byte frame[GSM_FRAME_BYTES];
    memcpy(frame, d + RTP_HEADER + f * GSM_FRAME_BYTES, GSM_FRAME_BYTES);
    gsm_decode((gsm)dec_, frame, pcm_ + f * 160);
  }
  if (!rxActive_) {
    rxActive_ = true;
    gsm_destroy((gsm)dec_);  // fresh decoder state per over
    dec_ = gsm_create();
    li_.onFrnVoiceStart(rcall_.c_str());
  }
  lastRxAudio_ = lastAudio_ = now_;
  li_.onFrnVoice(pcm_, PACKET_SAMPLES);
}

bool Client::sendVoice(const int16_t* pcm640, uint32_t nowMs) {
  now_ = nowMs;
  if (qso_ != QSO_CONNECTED) return false;
  uint8_t pkt[RTP_HEADER + FRAMES_PER_PACKET * GSM_FRAME_BYTES];
  pkt[0] = 0xC0;
  pkt[1] = 0x03;
  pkt[2] = (uint8_t)(seq_ >> 8);
  pkt[3] = (uint8_t)seq_;
  seq_++;
  memset(pkt + 4, 0, 8);  // time, ssrc
  int16_t frame[160];
  for (int f = 0; f < FRAMES_PER_PACKET; f++) {
    memcpy(frame, pcm640 + f * 160, sizeof(frame));
    gsm_encode((gsm)enc_, frame, pkt + RTP_HEADER + f * GSM_FRAME_BYTES);
  }
  lastAudio_ = nowMs;
  return net_.sendAudio(rip_, pkt, sizeof(pkt));
}

// -------------------------------------------------------------- directory --
void Client::startDirJob(DirJob j, uint32_t now) {
  if (!dirIp_ || elapsed(now, dirIpAt_, 600000)) {
    dirIp_ = net_.resolve(cfg_.server.c_str());
    dirIpAt_ = now;
  }
  if (!dirIp_) {
    log("cannot resolve %s", cfg_.server.c_str());
    if (j == DJ_REGISTER) nextRegister_ = now + 60000;
    if (j == DJ_LOOKUP) {
      lookupId_ = 0;
      if (onConnectResult) onConnectResult(false, "");
    }
    return;
  }
  job_ = j;
  step_ = DS_CONNECTING;
  jobSince_ = now;
  dirLine_.clear();
  dirReply_.clear();
  listState_ = 0;
  scanned_ = 0;
  dirBytes_ = 0;
  log("directory %s %s (%s)", j == DJ_LOOKUP ? "lookup on" : "login to", ipToString(dirIp_).c_str(), net_.name());
  if (!net_.dirOpen(dirIp_)) dirDone(false);
}

void Client::dirDone(bool ok) {
  DirJob j = job_;
  job_ = DJ_NONE;
  net_.dirClose();
  if (j == DJ_REGISTER) {
    nextRegister_ = now_ + (ok ? REGISTER_EVERY_MS : 60000);
    if (!ok) {
      if (registered_) log("directory registration lost");
      registered_ = false;
    }
  } else if (j == DJ_LOOKUP && lookupId_) {
    if (step_ == DS_CONNECTING)
      log("node %u: directory connection failed", (unsigned)lookupId_);
    else if (listState_ < 2)
      log("node %u: no station list received (%u bytes: '%s')", (unsigned)lookupId_, (unsigned)dirBytes_,
          dirLine_.substr(0, 40).c_str());
    else if (listState_ == 6 || listCount_ <= 0)
      log("node %u not found in the directory (%u stations checked)", (unsigned)lookupId_, (unsigned)scanned_);
    else
      log("node %u: station list interrupted after %u stations", (unsigned)lookupId_, (unsigned)scanned_);
    lookupId_ = 0;
    if (onConnectResult) onConnectResult(false, "");
  }
}

void Client::handleDir(const uint8_t* d, size_t n) {
  dirBytes_ += n;
  if (job_ == DJ_REGISTER) {
    dirReply_.append((const char*)d, n);
    if (dirReply_.size() >= 2) {
      bool ok = dirReply_.compare(0, 2, "OK") == 0;
      if (ok) {
        if (!registered_) log("registered on the directory as %s%s", cfg_.callsign.c_str(), regBusy_ ? " (busy)" : "");
        registered_ = true;
      } else {
        std::string r = dirReply_.substr(0, 60);
        for (auto& c : r)
          if ((unsigned char)c < 32) c = ' ';
        log("directory refused the login: '%s'", r.c_str());
      }
      dirDone(ok);
    }
    return;
  }
  if (job_ != DJ_LOOKUP) return;
  for (size_t i = 0; i < n && job_ == DJ_LOOKUP; i++) {
    char c = (char)d[i];
    if (c == '\n') {
      handleDirLine(dirLine_);
      dirLine_.clear();
    } else if (dirLine_.size() < 200) {
      dirLine_ += c;
    }
  }
  // The list ends with "+++" and no newline: don't wait for the server to close.
  if (job_ == DJ_LOOKUP && listState_ >= 2 && dirLine_ == "+++") {
    std::string l = dirLine_;
    dirLine_.clear();
    handleDirLine(l);
  }
}

void Client::handleDirLine(const std::string& line) {
  switch (listState_) {
    case 0:
      if (line == "@@@") listState_ = 1;
      break;
    case 1:
      listCount_ = atol(line.c_str());
      listState_ = listCount_ > 0 ? 2 : 6;
      break;
    case 2:
      entCall_ = line;
      listState_ = 3;
      break;
    case 3:
      listState_ = 4;  // description, unused
      break;
    case 4:
      entId_ = (uint32_t)strtoul(line.c_str(), nullptr, 10);
      listState_ = 5;
      break;
    case 5: {
      scanned_++;
      if (entId_ == lookupId_ && entCall_ != " " && entCall_ != ".") {
        int a, b, c2, d2;
        if (sscanf(line.c_str(), "%d.%d.%d.%d", &a, &b, &c2, &d2) == 4) {
          uint32_t ip = ((uint32_t)a << 24) | ((uint32_t)b << 16) | ((uint32_t)c2 << 8) | (uint32_t)d2;
          std::string call = entCall_;
          lookupId_ = 0;
          job_ = DJ_NONE;
          net_.dirClose();
          startOutgoing(ip, call, now_);
          return;
        }
      }
      listState_ = (--listCount_ > 0) ? 2 : 6;
      break;
    }
    default:
      if (line.compare(0, 3, "+++") == 0) dirDone(false);
      break;
  }
}

// ------------------------------------------------------------------- QSO ---
bool Client::connectTo(uint32_t nodeId, uint32_t nowMs) {
  now_ = nowMs;
  if (!running_ || nodeId == 0) return false;
  if (qso_ != QSO_IDLE) disconnect(nowMs, "new connection requested");
  lookupId_ = nodeId;
  log("looking up node %u", (unsigned)nodeId);
  return true;
}

void Client::startOutgoing(uint32_t ip, const std::string& call, uint32_t now) {
  rip_ = ip;
  rcall_ = call;
  rname_.clear();
  retries_ = 0;
  log("calling %s (%s) via %s", call.c_str(), ipToString(ip).c_str(), net_.name());
  dbgPackets_ = 6;
  now_ = now;
  setQso(QSO_CONNECTING);
  sendSdes();
}

void Client::disconnect(uint32_t nowMs, const char* why) {
  now_ = nowMs;
  if (qso_ == QSO_IDLE) {
    if (lookupId_) {
      lookupId_ = 0;
      if (job_ == DJ_LOOKUP) {
        job_ = DJ_NONE;
        net_.dirClose();
      }
    }
    return;
  }
  uint8_t b[64];
  size_t n = makeBye(b);
  net_.sendCtrl(rip_, b, n);
  log("disconnected from %s (%s)", rcall_.c_str(), why);
  setQso(QSO_IDLE);
}

void Client::loop(uint32_t nowMs) {
  now_ = nowMs;
  if (!running_) return;
  net_.open(nowMs);  // no-op when already open (proxy reconnects by itself)
  net_.poll(nowMs);
  if (!net_.ready()) {
    netWasReady_ = false;
    return;
  }
  if (!netWasReady_) {
    // New (or different) proxy: the directory must see the address we now use,
    // otherwise the other stations refuse our calls.
    netWasReady_ = true;
    nextRegister_ = nowMs;
  }

  // Directory: lookups first, then the periodic registration.
  if (job_ == DJ_NONE) {
    if (lookupId_) {
      startDirJob(DJ_LOOKUP, nowMs);
    } else if ((int32_t)(nowMs - nextRegister_) >= 0) {
      regBusy_ = qso_ != QSO_IDLE;
      startDirJob(DJ_REGISTER, nowMs);
    }
  } else {
    if (step_ == DS_CONNECTING && net_.dirConnected()) {
      std::string cmd;
      if (job_ == DJ_LOOKUP) {
        cmd = "s";
      } else {
        std::string t = localTime ? localTime() : std::string("00:00");
        cmd = "l" + cfg_.callsign + "\xAC\xAC" + cfg_.password + "\r" + (regBusy_ ? "BUSY3.40(" : "ONLINE3.38(") + t + ")\r" +
              cfg_.location + "\r";
      }
      net_.dirSend((const uint8_t*)cmd.data(), cmd.size());
      step_ = DS_WAIT_REPLY;
    }
    if (job_ != DJ_NONE && elapsed(nowMs, jobSince_, DIR_TIMEOUT_MS)) {
      log("directory timeout (%s, %u bytes)", step_ == DS_CONNECTING ? "no connection" : "waiting for data",
          (unsigned)dirBytes_);
      dirDone(false);
    }
  }

  // QSO timers
  if (qso_ == QSO_CONNECTING && elapsed(nowMs, lastSdesTx_, KEEPALIVE_MS)) {
    if (++retries_ >= CONNECT_RETRIES) {
      log("%s does not answer (no reply to %d calls) - try another proxy or direct UDP", rcall_.c_str(), CONNECT_RETRIES);
      setQso(QSO_IDLE);
      if (onConnectResult) onConnectResult(false, rcall_);
    } else {
      log("no answer from %s yet, call %d/%d", rcall_.c_str(), retries_ + 1, CONNECT_RETRIES);
      sendSdes();
    }
  } else if (qso_ == QSO_CONNECTED) {
    if (elapsed(nowMs, lastSdesTx_, KEEPALIVE_MS)) sendSdes();
    if (elapsed(nowMs, lastSdesRx_, CON_TIMEOUT_MS)) {
      log("%s: connection timeout", rcall_.c_str());
      setQso(QSO_IDLE);
    } else if (cfg_.idleTimeoutS && elapsed(nowMs, lastAudio_, cfg_.idleTimeoutS * 1000)) {
      disconnect(nowMs, "idle timeout");
    }
  }
  if (rxActive_ && elapsed(nowMs, lastRxAudio_, RX_END_MS)) {
    rxActive_ = false;
    li_.onFrnVoiceEnd();
  }
}

}  // namespace el
