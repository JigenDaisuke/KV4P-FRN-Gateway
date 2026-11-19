// FRN <-> radio gateway logic for the KV4P board.
//
//  audio task (core 1): owns I2S (ADC in RX, DAC in TX) and the PTT line.
//  net task   (core 0): owns the FRN client and the gateway state machine.
//  They exchange 8 kHz PCM through two FreeRTOS stream buffers.
#pragma once
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/stream_buffer.h>

#include "audio_io.h"
#include "cw.h"
#include "frn_client.h"
#include "radio_sa818.h"
#include "settings.h"
#include "tones.h"

struct GatewayStatus {
  char gw[16];
  char frn[24];
  char talker[64];
  char lastLocal[24];
  bool ptt = false, carrier = false, wifi = false, radioOk = false;
  int rssi = 0;
  int rxLevel = 0;
  int adcRate = 0;
  int clients = 0;
  uint32_t voiceRx = 0, voiceTx = 0, reconnects = 0, logins = 0, badFrames = 0;
};

class WiFiTransport;

class Gateway : public frn::Listener {
 public:
  void begin(const Settings& s, RadioSA818& radio);
  void getStatus(GatewayStatus& out);
  // Wi-Fi up/down notifications from the main loop.
  void setNetworkUp(bool up) { netUp_ = up; }
  // Plays a courtesy tone on air (web "Prova" buttons): 1 = end of FRN over,
  // 2 = gateway taken (local over sent), 3 = gateway busy. Ignored if not idle.
  void requestTestTone(int kind) { testTone_ = kind; }
  // Before a reboot: release the PTT and close the FRN session cleanly, so the
  // server does not see a duplicate login (AL=DUPL) when we come back.
  void requestShutdown() { shutdownReq_ = true; }
  bool shutdownDone() const { return shutdownDone_; }

  // frn::Listener
  void onFrnState(frn::State s) override;
  void onFrnLog(const char* msg) override;
  void onFrnVoiceStart(const char* who) override;
  void onFrnVoice(const int16_t* pcm, size_t samples) override;
  void onFrnVoiceEnd() override;
  void onFrnTxApproved() override;
  void onFrnText(const char* line) override;

 private:
  enum class GwState : uint8_t { Idle, RfToNet, NetToRf, Lockout };
  enum class TxPhase : uint8_t { KeyUp, Playing, Draining, Tone, CwId, Tail };
  enum ToneKind : uint8_t { TONE_NONE = 0, TONE_QSO_END = 1, TONE_LOCAL = 2, TONE_BUSY = 3 };

  static void audioTaskEntry(void* arg);
  static void netTaskEntry(void* arg);
  void audioTask();
  void netTask();
  void tick(uint32_t now);
  bool squelchOpen(uint32_t now);
  void startRfToNet(uint32_t now);
  void pumpRfToNet(uint32_t now, bool final);
  void endRfToNet(uint32_t now, const char* why);
  void startNetToRf(uint32_t now);
  void endNetToRf(uint32_t now, const char* why);
  void startCourtesy(uint32_t now, uint8_t kind);
  bool startTone(uint8_t kind);
  void afterTone(uint32_t now);
  void pumpGenerator(bool cw);
  void updateLed(uint32_t now);
  static const char* gwName(GwState s);

  Settings cfg_;
  RadioSA818* radio_ = nullptr;
  AudioIO audio_;
  WiFiTransport* transport_ = nullptr;
  frn::Client* frn_ = nullptr;
  CwGenerator cw_;
  ToneSequence tone_;
  uint8_t pendingTone_ = TONE_NONE;  // tone to play when the current local TX drains
  uint32_t rfPacketsSent_ = 0;       // packets that really went out in this local over
  volatile int testTone_ = 0;
  volatile bool shutdownReq_ = false;
  volatile bool shutdownDone_ = false;

  StreamBufferHandle_t capture_ = nullptr;  // radio -> net
  StreamBufferHandle_t play_ = nullptr;     // net -> radio

  // Shared with the audio task
  volatile bool wantTx_ = false;
  volatile bool playEnable_ = false;
  volatile uint32_t heartbeat_ = 0;
  volatile int level_ = 0;

  // Net task state
  volatile bool netUp_ = false;
  bool frnStarted_ = false;
  GwState gs_ = GwState::Idle;
  TxPhase phase_ = TxPhase::KeyUp;
  uint32_t gsSince_ = 0, phaseSince_ = 0;
  uint32_t sqCandidate_ = 0, sqLastSeen_ = 0;
  bool sqWasOpen_ = false;
  bool totLocked_ = false;      // local station exceeded TOT: wait for carrier drop
  bool netVoiceActive_ = false; // remote over in progress (from FRN)
  bool netVoiceIgnored_ = false;
  uint32_t lockoutUntil_ = 0;
  uint32_t lastCwId_ = 0;
  bool cwEverSent_ = false;
  uint32_t lastFrnLoop_ = 0;
  char talker_[64] = "";
  char lastLocal_[24] = "";

  SemaphoreHandle_t statusMtx_ = nullptr;
  GatewayStatus st_;
};
