#include "gateway.h"

#include <WiFi.h>
#include <string.h>

#include "board_kv4p.h"
#include "logbuf.h"

// ---------------------------------------------------------------------------
// TCP transport on top of WiFiClient
// ---------------------------------------------------------------------------
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

// ---------------------------------------------------------------------------
namespace {
constexpr size_t CAPTURE_BYTES = 8000 * 2 * 2;  // 2 s @ 8 kHz int16
constexpr size_t PLAY_BYTES = 8000 * 2 * 3;     // 3 s
constexpr uint32_t AUDIO_WATCHDOG_MS = 3000;    // net task silent -> force PTT off
int16_t txPcm[frn::PACKET_SAMPLES];             // net task only
volatile int32_t skipSamples = 0;               // net -> audio: drop backlog

inline bool before(uint32_t now, uint32_t t) { return (int32_t)(now - t) < 0; }

void discard(StreamBufferHandle_t sb, size_t bytes) {
  uint8_t tmp[256];
  while (bytes > 0) {
    size_t c = bytes < sizeof(tmp) ? bytes : sizeof(tmp);
    size_t r = xStreamBufferReceive(sb, tmp, c, 0);
    if (r == 0) break;
    bytes -= r;
  }
}
}  // namespace

const char* Gateway::gwName(GwState s) {
  switch (s) {
    case GwState::Idle: return "IDLE";
    case GwState::RfToNet: return "RADIO->FRN";
    case GwState::NetToRf: return "FRN->RADIO";
    case GwState::Lockout: return "LOCKOUT";
  }
  return "?";
}

void Gateway::begin(const Settings& s, RadioSA818& radio) {
  cfg_ = s;
  radio_ = &radio;
  statusMtx_ = xSemaphoreCreateMutex();
  capture_ = xStreamBufferCreate(CAPTURE_BYTES, 2);
  play_ = xStreamBufferCreate(PLAY_BYTES, 2);

  frn::Config fc;
  fc.server = s.frnServer.c_str();
  fc.port = s.frnPort;
  fc.backupServer = s.frnBackupServer.c_str();
  fc.backupPort = s.frnBackupPort;
  fc.version = s.frnVersion.c_str();
  fc.email = s.frnEmail.c_str();
  fc.password = s.frnPassword.c_str();
  // Same format as the official client: "CALL, Name" (always with the comma).
  String call = s.frnCallsign;
  call.toUpperCase();
  String on = call + ", " + s.frnName;
  fc.callsignAndUser = on.c_str();
  fc.clientType = String((int)s.frnClientType).c_str();  // 0 crosslink, 1 gateway, 2 PC only
  fc.bandAndChannel = settings::bandChannel(s).c_str();
  fc.description = s.frnDescription.c_str();
  fc.country = s.frnCountry.c_str();
  fc.city = s.frnCity.c_str();
  fc.net = s.frnNet.c_str();

  currentRoom_ = s.frnNet;
  transport_ = new WiFiTransport();
  frn_ = new frn::Client(*transport_, *this);
  frn_->setConfig(fc);

  audio_.rxGain = s.rxGain;
  audio_.txGain = s.txGain;

  heartbeat_ = millis();
  xTaskCreatePinnedToCore(audioTaskEntry, "audio", 8192, this, 10, nullptr, 1);
  xTaskCreatePinnedToCore(netTaskEntry, "frn", 12288, this, 5, nullptr, 0);
}

void Gateway::audioTaskEntry(void* arg) { static_cast<Gateway*>(arg)->audioTask(); }
void Gateway::netTaskEntry(void* arg) { static_cast<Gateway*>(arg)->netTask(); }

// ---------------------------------------------------------------------------
// Audio task: owns I2S and PTT
// ---------------------------------------------------------------------------
void Gateway::audioTask() {
  audio_.begin();
  int16_t buf[160];
  int64_t sumSq = 0;
  int nSq = 0;
  for (;;) {
    bool tx = wantTx_;
    if (tx && (uint32_t)(millis() - heartbeat_) > AUDIO_WATCHDOG_MS) {
      tx = false;
      wantTx_ = false;
      LOGF("SAFETY: net task stalled, PTT released");
    }
    if (tx && audio_.mode() != AudioIO::Mode::Tx) {
      audio_.setMode(AudioIO::Mode::Tx);  // DAC on first, then key: no pop on air
      radio_->setPtt(true);
    } else if (!tx && audio_.mode() == AudioIO::Mode::Tx) {
      radio_->setPtt(false);
      audio_.setMode(AudioIO::Mode::Rx);
    }

    if (audio_.mode() == AudioIO::Mode::Rx) {
      size_t n = audio_.readRx8k(buf, 64);
      for (size_t i = 0; i < n; i++) {
        sumSq += (int32_t)buf[i] * buf[i];
        if (++nSq >= 160) {  // 20 ms RMS
          level_ = (int)sqrtf((float)(sumSq / nSq));
          sumSq = 0;
          nSq = 0;
        }
      }
      if (n) xStreamBufferSend(capture_, buf, n * sizeof(int16_t), 0);  // drop if full
    } else {
      size_t got = 0;
      if (playEnable_) {
        int32_t skip = skipSamples;
        if (skip > 0) {
          discard(play_, (size_t)skip * 2);
          skipSamples = 0;
        }
        got = xStreamBufferReceive(play_, buf, sizeof(buf), 0) / sizeof(int16_t);
      }
      for (size_t i = got; i < 160; i++) buf[i] = 0;
      audio_.writeTx8k(buf, 160);  // blocks ~20 ms
      level_ = 0;
    }
  }
}

// ---------------------------------------------------------------------------
// Net task: FRN client + gateway logic
// ---------------------------------------------------------------------------
void Gateway::netTask() {
  uint32_t lastStatus = 0;
  for (;;) {
    uint32_t now = millis();
    heartbeat_ = now;
    if (shutdownReq_) {
      if (!shutdownDone_) {
        wantTx_ = false;
        playEnable_ = false;
        if (frnStarted_) {
          frn_->endTx(now);
          frn_->stop();  // closes the TCP session (FIN) so the server drops it at once
          frnStarted_ = false;
        }
        LOGF("shutdown: FRN session closed");
        shutdownDone_ = true;
      }
      vTaskDelay(pdMS_TO_TICKS(20));
      continue;
    }
    if (netUp_ && !frnStarted_) {
      frn_->start(now);
      frnStarted_ = true;
    } else if (!netUp_ && frnStarted_) {
      if (gs_ == GwState::RfToNet) endRfToNet(now, "Wi-Fi down");
      frn_->stop();
      frnStarted_ = false;
    }
    if (frnStarted_) frn_->loop(now);
    now = millis();
    heartbeat_ = now;
    tick(now);
    updateLed(now);

    if (now - lastStatus > 250) {
      lastStatus = now;
      xSemaphoreTake(statusMtx_, portMAX_DELAY);
      strncpy(st_.gw, gwName(gs_), sizeof(st_.gw) - 1);
      strncpy(st_.frn, frn::stateName(frn_->state()), sizeof(st_.frn) - 1);
      strncpy(st_.talker, talker_, sizeof(st_.talker) - 1);
      strncpy(st_.lastLocal, lastLocal_, sizeof(st_.lastLocal) - 1);
      strncpy(st_.room, currentRoom_.c_str(), sizeof(st_.room) - 1);
      st_.ptt = radio_->ptt();
      st_.carrier = radio_->carrier();
      st_.radioOk = radio_->found();
      st_.wifi = WiFi.status() == WL_CONNECTED;
      st_.rssi = st_.wifi ? WiFi.RSSI() : 0;
      st_.rxLevel = level_;
      st_.adcRate = (int)audio_.adcRateHz();
      st_.clients = (int)frn_->clients().size();
      const auto& s = frn_->stats();
      st_.voiceRx = s.voiceRx;
      st_.voiceTx = s.voiceTx;
      st_.reconnects = s.reconnects;
      st_.logins = s.logins;
      st_.badFrames = s.badFrames;
      xSemaphoreGive(statusMtx_);
    }
    vTaskDelay(pdMS_TO_TICKS(4));
  }
}

void Gateway::getStatus(GatewayStatus& out) {
  if (!statusMtx_) return;
  xSemaphoreTake(statusMtx_, portMAX_DELAY);
  out = st_;
  xSemaphoreGive(statusMtx_);
}

bool Gateway::squelchOpen(uint32_t now) {
  if (before(now, lockoutUntil_)) return false;
  if (audio_.mode() != AudioIO::Mode::Rx) return false;
  bool hw = radio_->carrier();
  bool vox = level_ > (int)cfg_.voxThreshold;
  switch (cfg_.sqMode) {
    case 1: return vox;
    case 2: return hw && vox;
    default: return hw;
  }
}

void Gateway::tick(uint32_t now) {
  // --- local squelch with open-delay and hang time ---
  bool sq = squelchOpen(now);
  if (sq) {
    sqLastSeen_ = now;
    if (!sqWasOpen_) {
      sqCandidate_ = now;
      sqWasOpen_ = true;
    }
  } else {
    sqWasOpen_ = false;
  }
  const bool sqStable = sq && (now - sqCandidate_ >= cfg_.sqOpenMs);
  const bool sqHeld = (now - sqLastSeen_) < cfg_.sqHangMs;

  switch (gs_) {
    case GwState::Idle:
    case GwState::Lockout: {
      if (totLocked_ && !sqHeld) {
        totLocked_ = false;
        LOGF("TOT reset (carrier dropped)");
      }
      if (netVoiceActive_ && !netVoiceIgnored_) {
        startNetToRf(now);
      } else if (testTone_) {
        int k = testTone_;
        testTone_ = 0;
        LOGF("test tone %d requested from the web page", k);
        startCourtesy(now, (uint8_t)k);
      } else if (!totLocked_ && sqStable && frnStarted_) {
        startRfToNet(now);  // also when not logged in: DTMF commands must still work
      } else {
        if (cfg_.dtmfReturnMin && currentRoom_ != cfg_.frnNet &&
            now - lastTraffic_ > (uint32_t)cfg_.dtmfReturnMin * 60000UL)
          switchRoom(cfg_.frnNet, now, "inactivity, back to the main room");
        // keep only a short pre-roll so the first syllable is not lost
        size_t keep = (size_t)(cfg_.sqOpenMs + 150) * 8 * sizeof(int16_t);
        size_t avail = xStreamBufferBytesAvailable(capture_);
        if (avail > keep) discard(capture_, avail - keep);
      }
      break;
    }

    case GwState::RfToNet:
      if (!cmdMode_ && frn_->loggedIn() && !frn_->txWanted()) frn_->requestTx(now);  // e.g. after a reconnect
      pumpRfToNet(now, false);
      if (!sqHeld) {
        endRfToNet(now, "carrier dropped");
      } else if (now - gsSince_ > (uint32_t)cfg_.totSec * 1000) {
        endRfToNet(now, "TIME-OUT");
        totLocked_ = true;
      }
      break;

    case GwState::NetToRf: {
      size_t buffered = xStreamBufferBytesAvailable(play_) / sizeof(int16_t);
      const size_t jitter = (size_t)cfg_.jitterMs * 8;
      if (phase_ < TxPhase::CwId && now - gsSince_ > (uint32_t)cfg_.totSec * 1000) {
        netVoiceIgnored_ = true;  // drop the rest of this over
        endNetToRf(now, "TIME-OUT");
        break;
      }
      switch (phase_) {
        case TxPhase::KeyUp:
          if (now - phaseSince_ >= cfg_.txDelayMs && (buffered >= jitter || !netVoiceActive_)) {
            playEnable_ = true;
            phase_ = netVoiceActive_ ? TxPhase::Playing : TxPhase::Draining;
            phaseSince_ = now;
          }
          break;
        case TxPhase::Playing:
          // Keep latency bounded if the DAC clock is a bit slower than 8 kHz.
          if (buffered > jitter + 8000 && skipSamples == 0) skipSamples = (int32_t)(buffered - jitter);
          break;
        case TxPhase::Draining:
          if (buffered == 0 && now - phaseSince_ > 60) {
            if (startTone(pendingTone_)) {
              phase_ = TxPhase::Tone;
              phaseSince_ = now;
            } else {
              afterTone(now);
            }
            pendingTone_ = TONE_NONE;
          }
          break;
        case TxPhase::Tone:
          pumpGenerator(false);
          // Re-read the buffer AFTER generating: the whole tone may have just
          // been queued and still has to be played (bug in 0.3.0/0.3.1).
          if (tone_.done() && xStreamBufferBytesAvailable(play_) == 0) {
            if (now - phaseSince_ < 40) break;  // let the DAC drain its last DMA buffers
            afterTone(now);
          } else {
            phaseSince_ = now;
          }
          break;
        case TxPhase::CwId:
          pumpGenerator(true);
          if (cw_.done() && xStreamBufferBytesAvailable(play_) == 0) {
            if (cwIsId_) {
              lastCwId_ = now;
              cwEverSent_ = true;
              LOGF("CW ID sent (%s)", cfg_.frnCallsign.c_str());
              phase_ = netVoiceActive_ ? TxPhase::Playing : TxPhase::Tail;
              phaseSince_ = now;
            } else {
              afterTone(now);  // code sent: now the ID if it is due
            }
          }
          break;
        case TxPhase::Tail:
          if (now - phaseSince_ >= cfg_.txTailMs) endNetToRf(now, "end of over");
          break;
      }
      break;
    }
  }
}

// --- courtesy tones / CW ---------------------------------------------------
void Gateway::pumpGenerator(bool cw) {
  int16_t tmp[160];
  while (xStreamBufferSpacesAvailable(play_) >= sizeof(tmp)) {
    size_t n = cw ? cw_.fill(tmp, 160) : tone_.fill(tmp, 160);
    if (n == 0) break;
    xStreamBufferSend(play_, tmp, n * sizeof(int16_t), 0);
  }
}

bool Gateway::presetFor(uint8_t kind, uint8_t& preset, uint16_t& hz) const {
  switch (kind) {
    case TONE_QSO_END: preset = cfg_.toneQsoEnd; hz = cfg_.toneQsoEndHz; break;
    case TONE_LOCAL: preset = cfg_.toneLocal; hz = cfg_.toneLocalHz; break;
    case TONE_BUSY: preset = cfg_.toneBusy; hz = cfg_.toneBusyHz; break;
    case TONE_CMD_OK: preset = 5; hz = 1000; break;   // three rising tones (+ the code in CW)
    case TONE_CMD_ERR: preset = 4; hz = 450; break;   // falling, low: unknown code
    default: return false;
  }
  return preset != 0;
}

bool Gateway::startTone(uint8_t kind) {
  uint8_t preset = 0;
  uint16_t hz = 1000;
  if (!presetFor(kind, preset, hz)) return false;
  tone_.start(preset, hz, cfg_.toneLevel);
  return !tone_.done();
}

// After the courtesy tone: CW identification if due, then the TX tail.
void Gateway::afterTone(uint32_t now) {
  if (netVoiceActive_) {  // the next station started meanwhile: keep the PTT down
    phase_ = TxPhase::Playing;
    phaseSince_ = now;
    return;
  }
  if (pendingCw_[0]) {  // confirmation of a DTMF command: the code in CW
    cw_.start(pendingCw_, cfg_.cwWpm, cfg_.cwToneHz, cfg_.cwLevel, 120, 100);
    pendingCw_[0] = 0;
    cwIsId_ = false;
    phase_ = TxPhase::CwId;
    phaseSince_ = now;
    return;
  }
  bool due = cfg_.cwIdEnabled && cfg_.frnCallsign.length() > 0 &&
             (!cwEverSent_ || now - lastCwId_ >= (uint32_t)cfg_.cwIdMinutes * 60000UL);
  if (due) {
    cw_.start(cfg_.frnCallsign.c_str(), cfg_.cwWpm, cfg_.cwToneHz, cfg_.cwLevel);
    cwIsId_ = true;
    phase_ = TxPhase::CwId;
  } else {
    phase_ = TxPhase::Tail;
  }
  phaseSince_ = now;
}

// Key up only to send a courtesy tone (after a local over, or a test).
void Gateway::startCourtesy(uint32_t now, uint8_t kind) {
  uint8_t preset = 0;
  uint16_t hz = 0;
  if (!presetFor(kind, preset, hz)) return;
  netVoiceIgnored_ = false;
  startNetToRf(now);
  pendingTone_ = kind;
  LOGF("courtesy tone: %s", tones::presetName(preset));
}

// --- radio -> FRN ---------------------------------------------------------
static bool trimPending = false;
namespace {
constexpr size_t PRE_MAX = 8000;  // 1 s of audio kept while waiting for the floor
int16_t preBuf[PRE_MAX];
size_t preHead = 0, preLen = 0;   // ring buffer (net task only)
size_t txFill = 0;                // samples already in txPcm
}  // namespace


void Gateway::startRfToNet(uint32_t now) {
  gs_ = GwState::RfToNet;
  gsSince_ = now;
  trimPending = false;
  rfPacketsSent_ = 0;
  cmdMode_ = false;
  cmdBuf_[0] = 0;
  dtmf_.reset();
  preLen = 0;
  txFill = 0;
  lastTraffic_ = now;
  if (frn_->loggedIn()) frn_->requestTx(now);
  LOGF("RADIO->FRN: carrier detected%s", frn_->loggedIn() ? ", requesting the floor" : " (FRN not connected)");
}

void Gateway::onFrnTxApproved() {
  trimPending = true;
  LOGF("RADIO->FRN: floor granted");
}

// Every 20 ms chunk from the radio goes through the DTMF decoder first, then
// into a 1 s pre-buffer, and from there into 200 ms FRN packets once the
// server has granted the floor.
void Gateway::pumpRfToNet(uint32_t now, bool final) {
  int16_t chunk[160];
  while (xStreamBufferBytesAvailable(capture_) >= sizeof(chunk)) {
    xStreamBufferReceive(capture_, chunk, sizeof(chunk), 0);
    handleChunk(chunk, 160, now);
  }
  if (!final) return;
  size_t rem = xStreamBufferReceive(capture_, chunk, sizeof(chunk), 0) / sizeof(int16_t);
  if (rem) handleChunk(chunk, rem, now);
  if (!cmdMode_ && frn_->txActive()) {
    drainPre(now);
    if (txFill > 0) {
      for (size_t i = txFill; i < (size_t)frn::PACKET_SAMPLES; i++) txPcm[i] = 0;
      if (frn_->sendVoice(txPcm, now)) rfPacketsSent_++;
    }
  }
  preLen = 0;
  txFill = 0;
}

void Gateway::handleChunk(const int16_t* c, size_t n, uint32_t now) {
  if (cfg_.dtmfEnabled) {
    char d = dtmf_.process(c, (int)n);
    if (d) onDtmf(d, now);
  }
  if (cmdMode_) return;  // command in progress: nothing goes to FRN
  for (size_t i = 0; i < n; i++) {
    size_t pos = (preHead + preLen) % PRE_MAX;
    preBuf[pos] = c[i];
    if (preLen < PRE_MAX) preLen++;
    else preHead = (preHead + 1) % PRE_MAX;  // full: drop the oldest sample
  }
  if (frn_->txActive()) drainPre(now);
}

void Gateway::drainPre(uint32_t now) {
  if (trimPending) {  // floor just granted: limit start-up latency to ~400 ms
    trimPending = false;
    while (preLen > 3200) {
      preHead = (preHead + 1) % PRE_MAX;
      preLen--;
    }
  }
  while (preLen > 0) {
    txPcm[txFill++] = preBuf[preHead];
    preHead = (preHead + 1) % PRE_MAX;
    preLen--;
    if (txFill == (size_t)frn::PACKET_SAMPLES) {
      txFill = 0;
      if (!frn_->sendVoice(txPcm, now)) return;
      rfPacketsSent_++;
    }
  }
}

// "#" opens a command, digits follow, "*" clears; the end of the over runs it.
void Gateway::onDtmf(char d, uint32_t now) {
  if (d == '#') {
    if (!cmdMode_) {
      cmdMode_ = true;
      preLen = 0;   // drop the "#" tone not yet sent
      txFill = 0;
      frn_->endTx(now);  // give the floor back: the others must not hear the tones
      LOGF("DTMF: command started");
    }
    cmdBuf_[0] = 0;
    return;
  }
  if (!cmdMode_) return;
  size_t l = strlen(cmdBuf_);
  if (d == '*') {
    cmdBuf_[0] = 0;
  } else if (d >= '0' && d <= '9' && l < sizeof(cmdBuf_) - 1) {
    cmdBuf_[l] = d;
    cmdBuf_[l + 1] = 0;
  }
  LOGF("DTMF: #%s", cmdBuf_);
}

void Gateway::executeCommand(uint32_t now) {
  String code = cmdBuf_;
  String room;
  if (code == "0") {
    room = cfg_.frnNet;
  } else if (code.length()) {
    for (int i = 0; i < DTMF_SLOTS; i++)
      if (cfg_.dtmfCode[i] == code && cfg_.dtmfRoom[i].length()) room = cfg_.dtmfRoom[i];
  }
  if (!room.length()) {
    LOGF("DTMF #%s: unknown code", cmdBuf_);
    startCourtesy(now, TONE_CMD_ERR);
    return;
  }
  LOGF("DTMF #%s -> room '%s'", cmdBuf_, room.c_str());
  strncpy(pendingCw_, cmdBuf_, sizeof(pendingCw_) - 1);
  pendingCw_[sizeof(pendingCw_) - 1] = 0;
  startCourtesy(now, TONE_CMD_OK);
  if (room != currentRoom_) switchRoom(room, now, "DTMF");
}

// FRN has no "join": close the session and log in again with the new room.
void Gateway::switchRoom(const String& room, uint32_t now, const char* why) {
  currentRoom_ = room;
  lastTraffic_ = now;
  frn::Config fc = frn_->config();
  fc.net = room.c_str();
  frn_->setConfig(fc);
  LOGF("room change -> '%s' (%s)", room.c_str(), why);
  if (frnStarted_) frn_->start(now, 1500);  // short pause so the server drops the old session
}

void Gateway::endRfToNet(uint32_t now, const char* why) {
  pumpRfToNet(now, true);
  frn_->endTx(now);
  lastTraffic_ = now;
  uint32_t dur = now - gsSince_;
  LOGF("RADIO->FRN end (%s), %lu.%lu s", why, (unsigned long)(dur / 1000), (unsigned long)((dur % 1000) / 100));
  snprintf(lastLocal_, sizeof(lastLocal_), "%lu.%lu s, %s", (unsigned long)(dur / 1000),
           (unsigned long)((dur % 1000) / 100), why);
  gs_ = GwState::Idle;
  gsSince_ = now;
  if (cmdMode_) {  // the over was a DTMF command
    cmdMode_ = false;
    dtmf_.reset();
    executeCommand(now);
    return;
  }
  dtmf_.reset();
  // "Coda": tell the local station whether its over really went out on FRN.
  const bool timeout = strcmp(why, "TIME-OUT") == 0;
  if (strcmp(why, "Wi-Fi down") != 0) {
    if (rfPacketsSent_ > 0 && !timeout) startCourtesy(now, TONE_LOCAL);
    else startCourtesy(now, TONE_BUSY);
  }
}

// --- FRN -> radio ---------------------------------------------------------
void Gateway::startNetToRf(uint32_t now) {
  xStreamBufferReset(play_);  // audio task is not reading (playEnable_ false)
  skipSamples = 0;
  playEnable_ = false;
  wantTx_ = true;
  gs_ = GwState::NetToRf;
  phase_ = TxPhase::KeyUp;
  gsSince_ = phaseSince_ = now;
  pendingTone_ = TONE_QSO_END;
  lastTraffic_ = now;
  if (netVoiceActive_) LOGF("FRN->RADIO: %s", talker_);
}

void Gateway::endNetToRf(uint32_t now, const char* why) {
  playEnable_ = false;
  wantTx_ = false;
  uint32_t dur = now - gsSince_;
  LOGF("FRN->RADIO end (%s), %lu.%lu s", why, (unsigned long)(dur / 1000), (unsigned long)((dur % 1000) / 100));
  lockoutUntil_ = now + cfg_.rxLockoutMs;
  gs_ = GwState::Idle;
  gsSince_ = now;
}

void Gateway::onFrnVoiceStart(const char* who) {
  strncpy(talker_, who, sizeof(talker_) - 1);
  talker_[sizeof(talker_) - 1] = 0;
  netVoiceActive_ = true;
  uint32_t now = millis();
  if (gs_ == GwState::Idle || gs_ == GwState::Lockout) {
    netVoiceIgnored_ = false;
    startNetToRf(now);
  } else if (gs_ == GwState::NetToRf) {
    if (phase_ == TxPhase::Draining || phase_ == TxPhase::Tail || phase_ == TxPhase::KeyUp) {
      if (phase_ != TxPhase::KeyUp) phase_ = TxPhase::Playing;  // next station right away: keep the PTT down
      phaseSince_ = now;
      pendingTone_ = TONE_QSO_END;
      LOGF("FRN->RADIO: %s", talker_);
    }
  } else {
    netVoiceIgnored_ = true;  // local station is talking: don't double
    LOGF("FRN voice from %s ignored (radio busy)", who);
  }
}

void Gateway::onFrnVoice(const int16_t* pcm, size_t samples) {
  if (gs_ != GwState::NetToRf || netVoiceIgnored_) return;
  if (phase_ == TxPhase::Tone || phase_ == TxPhase::CwId || phase_ == TxPhase::Tail) return;
  size_t bytes = samples * sizeof(int16_t);
  if (xStreamBufferSpacesAvailable(play_) >= bytes) xStreamBufferSend(play_, pcm, bytes, 0);
}

void Gateway::onFrnVoiceEnd() {
  netVoiceActive_ = false;
  netVoiceIgnored_ = false;
  // (In KeyUp the tick() handler starts playback after the TX delay, then drains.)
  if (gs_ == GwState::NetToRf && phase_ == TxPhase::Playing) {
    phase_ = TxPhase::Draining;
    phaseSince_ = millis();
  }
}

void Gateway::onFrnState(frn::State s) {
  if (s == frn::State::Idle || s == frn::State::Disconnected || s == frn::State::LoginRejected ||
      s == frn::State::Connecting)
    LOGF("FRN state: %s", frn::stateName(s));
}

void Gateway::onFrnLog(const char* msg) { LOGF("FRN: %s", msg); }

void Gateway::onFrnText(const char* line) { LOGF("FRN text: %s", line); }

// --- LED ------------------------------------------------------------------
void Gateway::updateLed(uint32_t now) {
  bool on;
  if (gs_ == GwState::RfToNet || gs_ == GwState::NetToRf)
    on = (now / 60) & 1;                     // flicker: traffic
  else if (!netUp_)
    on = (now / 500) & 1;                    // slow: no Wi-Fi
  else if (!frnStarted_ || !frn_->loggedIn())
    on = (now / 100) & 1;                    // fast: no FRN login
  else
    on = true;                               // solid: ready
  digitalWrite(board::PIN_LED, on ? HIGH : LOW);
}
