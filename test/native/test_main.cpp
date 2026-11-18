// Native (PC) tests for the portable parts: FRN protocol state machine,
// GSM/WAV49 codec, resamplers, CW generator.
//   see test/native/run.sh
#include <math.h>
#include <stdio.h>
#include <string.h>

#include <deque>
#include <string>
#include <vector>

#include "cw.h"
#include "frn_client.h"
#include "frn_codec.h"
#include "resampler.h"

static int failures = 0, checks = 0;
#define CHECK(cond)                                                    \
  do {                                                                 \
    checks++;                                                          \
    if (!(cond)) {                                                     \
      failures++;                                                      \
      printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);         \
    }                                                                  \
  } while (0)

// ---------------------------------------------------------------- mocks
struct MockTransport : frn::Transport {
  bool up = false, connectOk = true;
  int connects = 0;
  std::string lastHost;
  uint16_t lastPort = 0;
  std::deque<uint8_t> rx;  // server -> client
  std::string tx;          // client -> server
  size_t chunk = 100000;   // max bytes returned per read (fragmentation tests)
  bool connect(const char* h, uint16_t p) override {
    connects++;
    lastHost = h;
    lastPort = p;
    up = connectOk;
    return up;
  }
  void close() override { up = false; rx.clear(); }
  bool connected() override { return up; }
  int read(uint8_t* b, size_t m) override {
    size_t n = 0;
    while (n < m && n < chunk && !rx.empty()) {
      b[n++] = rx.front();
      rx.pop_front();
    }
    return (int)n;
  }
  size_t write(const uint8_t* b, size_t n) override {
    tx.append((const char*)b, n);
    return n;
  }
  void serverSend(const std::string& s) { for (char c : s) rx.push_back((uint8_t)c); }
  void serverSend(const uint8_t* p, size_t n) { for (size_t i = 0; i < n; i++) rx.push_back(p[i]); }
  std::string take() {
    std::string t = tx;
    tx.clear();
    return t;
  }
};

struct MockListener : frn::Listener {
  std::vector<std::string> logs, texts;
  std::string voiceFrom;
  int voiceStarts = 0, voiceEnds = 0, txApproved = 0;
  size_t voiceSamples = 0;
  std::vector<int16_t> lastPcm;
  void onFrnLog(const char* m) override { logs.push_back(m); }
  void onFrnVoiceStart(const char* w) override { voiceStarts++; voiceFrom = w; }
  void onFrnVoice(const int16_t* p, size_t n) override {
    voiceSamples += n;
    lastPcm.assign(p, p + n);
  }
  void onFrnVoiceEnd() override { voiceEnds++; }
  void onFrnTxApproved() override { txApproved++; }
  void onFrnText(const char* l) override { texts.push_back(l); }
};

static void makeSine(int16_t* pcm, int n, double f, double amp, int offset = 0) {
  for (int i = 0; i < n; i++) pcm[i] = (int16_t)(amp * sin(2 * M_PI * f * (i + offset) / 8000.0));
}

// Feed loop() repeatedly at a fixed time.
static void spin(frn::Client& c, uint32_t t, int n = 5) {
  for (int i = 0; i < n; i++) c.loop(t);
}

static frn::Config testConfig() {
  frn::Config cfg;
  cfg.server = "server.voip-italia.net";
  cfg.port = 10024;
  cfg.email = "test@example.com";
  cfg.password = "ABC123";
  cfg.callsignAndUser = "IZ0TST, Test";
  cfg.bandAndChannel = "145.50000FM CTC88.5";
  cfg.city = "Roma - Centro";
  cfg.net = "Nazionale";
  return cfg;
}

static void loginOk(MockTransport& tr, frn::Client& c, uint32_t t) {
  tr.serverSend("2014003\r\n");
  tr.serverSend("<SV>2014003</SV><AL>OK</AL><BN>Server</BN>\r\n");
  spin(c, t);
}

// ---------------------------------------------------------------- tests
static void testLogin() {
  printf("[login]\n");
  MockTransport tr;
  MockListener li;
  frn::Client c(tr, li);
  c.setConfig(testConfig());
  c.start(0);
  spin(c, 1);
  CHECK(tr.connects == 1);
  CHECK(tr.lastHost == "server.voip-italia.net" && tr.lastPort == 10024);
  CHECK(c.state() == frn::State::LoggingIn1);
  std::string login = tr.take();
  const std::string expect =
      "CT:<VX>2014003</VX><EA>test@example.com</EA><PW>ABC123</PW><ON>IZ0TST, Test</ON><CL>1</CL>"
      "<BC>145.50000FM CTC88.5</BC><DS>KV4P ESP32 FRN Gateway</DS><NN>Italy</NN><CT>Roma - Centro</CT>"
      "<NT>Nazionale</NT>\r\n";
  CHECK(login == expect);
  if (login != expect) printf("  got: %s\n", login.c_str());

  // Answer split byte by byte
  tr.chunk = 1;
  tr.serverSend("2014003\r\n<SV>2014003</SV><AL>OK</AL>\r\n");
  for (int i = 0; i < 80; i++) c.loop(10);
  CHECK(c.state() == frn::State::Idle);
  CHECK(c.loggedIn());
  CHECK(tr.take() == "RX0\r\n");
  CHECK(c.stats().logins == 1);
}

static void testListsAndKeepalive() {
  printf("[lists, idle, keepalive, text]\n");
  MockTransport tr;
  MockListener li;
  frn::Client c(tr, li);
  c.setConfig(testConfig());
  c.start(0);
  spin(c, 1);
  tr.take();
  loginOk(tr, c, 2);
  tr.take();

  // Client list, fragmented in 3-byte reads, with a DT_IDLE right after.
  tr.chunk = 3;
  uint8_t hdr[] = {3, 0, 0};
  tr.serverSend(hdr, 3);
  tr.serverSend("2\r\n<ID>11</ID><ON>IZ0AAA, Mario</ON><BC>PC Only</BC>\r\n<ON>IZ0BBB, Luigi</ON><BC>145.5FM</BC>\r\n");
  uint8_t idle = 0;
  tr.serverSend(&idle, 1);
  for (int i = 0; i < 100; i++) c.loop(100);
  CHECK(c.clients().size() == 2);
  if (c.clients().size() == 2) {
    CHECK(c.clients()[0] == "IZ0AAA, Mario");
    CHECK(c.clients()[1] == "IZ0BBB, Luigi");
  }
  CHECK(c.state() == frn::State::Idle);
  CHECK(tr.take().empty());  // DT_IDLE is not answered (no P ping-pong)

  // Text message: sender id, text, kind
  tr.chunk = 100000;
  uint8_t t = 4;
  tr.serverSend(&t, 1);
  tr.serverSend("3\r\n11\r\nCiao dal server\r\nA\r\n");
  spin(c, 200);
  CHECK(li.texts.size() == 1 && li.texts[0] == "11: Ciao dal server");
  CHECK(c.state() == frn::State::Idle);

  // Keepalive: P every second, unconditionally
  tr.take();
  spin(c, 900);
  CHECK(tr.tx.empty());
  spin(c, 1010);
  CHECK(tr.take() == "P\r\n");
  spin(c, 1500);
  CHECK(tr.tx.empty());
  spin(c, 2020);
  CHECK(tr.take() == "P\r\n");

  // Several commands in one read: idle, net names list, idle, unknown byte
  uint8_t mix[] = {0, 5};
  tr.serverSend(mix, 2);
  tr.serverSend("2\r\nNazionale\r\nProve\r\n");
  uint8_t tail[] = {0, 77, 0};
  tr.serverSend(tail, 3);
  spin(c, 2100);
  CHECK(c.state() == frn::State::Idle);
  CHECK(tr.take().empty());
  CHECK(tr.rx.empty());
}

static void testVoiceRx() {
  printf("[voice from network]\n");
  MockTransport tr;
  MockListener li;
  frn::Client c(tr, li);
  c.setConfig(testConfig());
  c.start(0);
  spin(c, 1);
  loginOk(tr, c, 2);
  uint8_t hdr[] = {3, 0, 0};
  tr.serverSend(hdr, 3);
  tr.serverSend("2\r\n<ON>IZ0AAA, Mario</ON>\r\n<ON>IZ0BBB, Luigi</ON>\r\n");
  spin(c, 3);
  tr.take();

  frn::Codec enc;
  int16_t pcm[frn::PACKET_SAMPLES];
  uint8_t pkt[frn::PACKET_BYTES];
  uint32_t t = 100;
  for (int p = 0; p < 5; p++) {
    makeSine(pcm, frn::PACKET_SAMPLES, 600, 8000, p * frn::PACKET_SAMPLES);
    enc.encodePacket(pcm, pkt);
    uint8_t head[] = {2, 0, 2};  // DT_VOICE_BUFFER, client index 2 (1-based)
    tr.chunk = (p == 2) ? 7 : 100000;  // one packet heavily fragmented
    tr.serverSend(head, 3);
    tr.serverSend(pkt, sizeof(pkt));
    for (int i = 0; i < 80; i++) c.loop(t);
    t += 200;
  }
  CHECK(li.voiceStarts == 1);
  CHECK(li.voiceFrom == "IZ0BBB, Luigi");
  CHECK(li.voiceSamples == 5 * (size_t)frn::PACKET_SAMPLES);
  CHECK(c.stats().voiceRx == 5);
  CHECK(c.stats().badFrames == 0);
  std::string out = tr.take();
  CHECK(out == "P\r\nP\r\nP\r\nP\r\nP\r\n");  // P after each voice packet
  // decoded audio must be a reasonable copy of the 600 Hz tone
  double e = 0;
  for (auto s : li.lastPcm) e += (double)s * s;
  double rms = sqrt(e / li.lastPcm.size());
  CHECK(rms > 3500 && rms < 8000);  // ideal 5657
  printf("  decoded RMS %.0f (ideal 5657)\n", rms);

  CHECK(li.voiceEnds == 0);
  // another station takes over without a gap: new talker, fresh decoder
  uint8_t head2[] = {2, 0, 1};
  tr.serverSend(head2, 3);
  tr.serverSend(pkt, sizeof(pkt));
  spin(c, t);
  CHECK(li.voiceStarts == 2);
  CHECK(li.voiceFrom == "IZ0AAA, Mario");
  spin(c, t + 1100);
  CHECK(li.voiceEnds == 1);
}

static void testTx() {
  printf("[gateway TX: radio -> network]\n");
  MockTransport tr;
  MockListener li;
  frn::Client c(tr, li);
  c.setConfig(testConfig());
  c.start(0);
  spin(c, 1);
  loginOk(tr, c, 2);
  tr.take();

  c.requestTx(10);
  CHECK(tr.take() == "TX0\r\n");
  CHECK(c.state() == frn::State::TxWaiting);
  CHECK(!c.txActive());
  int16_t pcm[frn::PACKET_SAMPLES];
  makeSine(pcm, frn::PACKET_SAMPLES, 1000, 6000);
  CHECK(!c.sendVoice(pcm, 11));  // not granted yet

  uint8_t grant[] = {1, 0, 3};  // DT_DO_TX + client index
  tr.serverSend(grant, 3);
  spin(c, 20);
  CHECK(c.txActive());
  CHECK(li.txApproved == 1);
  CHECK(c.sendVoice(pcm, 30));
  std::string sent = tr.take();
  CHECK(sent.size() == 5 + frn::PACKET_BYTES);
  CHECK(sent.compare(0, 5, "TX1\r\n") == 0);
  // the payload must decode back to the tone
  frn::Codec dec;
  int16_t back[frn::PACKET_SAMPLES];
  CHECK(dec.decodePacket((const uint8_t*)sent.data() + 5, back));
  CHECK(c.sendVoice(pcm, 230));
  tr.take();
  uint8_t idle = 0;
  tr.serverSend(&idle, 1);  // idle marker during our over: must NOT end the TX
  spin(c, 231);
  CHECK(c.txActive());

  // long over: no data from the server for > 30 s must NOT drop us while we send
  uint32_t t = 230;
  for (int i = 0; i < 200; i++) {  // 40 s
    t += 200;
    c.sendVoice(pcm, t);
    c.loop(t);
  }
  CHECK(c.txActive());
  CHECK(c.stats().voiceTx == 202);
  std::string during = tr.take();
  CHECK(during.find("P\r\n") != std::string::npos);  // keepalive also while talking

  c.endTx(t);
  CHECK(tr.take() == "RX0\r\n");
  CHECK(c.state() == frn::State::Idle);
}

static void testTxDeniedAndLateGrant() {
  printf("[TX busy net / retry / cancel before grant]\n");
  MockTransport tr;
  MockListener li;
  frn::Client c(tr, li);
  c.setConfig(testConfig());
  c.start(0);
  spin(c, 1);
  loginOk(tr, c, 2);
  tr.take();

  c.requestTx(100);
  CHECK(tr.take() == "TX0\r\n");
  uint8_t idle = 0;
  tr.serverSend(&idle, 1);  // idle marker: NOT a denial
  spin(c, 110);
  CHECK(c.state() == frn::State::TxWaiting);
  CHECK(tr.tx.empty());
  spin(c, 2050);
  std::string out = tr.take();
  CHECK(out.find("TX0\r\n") == std::string::npos);  // keepalive only so far
  spin(c, 2110);
  CHECK(tr.take().find("TX0\r\n") != std::string::npos);  // retried after 2 s

  // remote voice arrives while we wait: no TX0 retries during it
  frn::Codec enc;
  int16_t pcm[frn::PACKET_SAMPLES];
  uint8_t pkt[frn::PACKET_BYTES];
  makeSine(pcm, frn::PACKET_SAMPLES, 500, 5000);
  enc.encodePacket(pcm, pkt);
  for (uint32_t t = 2200; t < 6000; t += 200) {
    uint8_t head[] = {2, 0, 0};
    tr.serverSend(head, 3);
    tr.serverSend(pkt, sizeof(pkt));
    spin(c, t);
  }
  CHECK(tr.take().find("TX0") == std::string::npos);

  // local over ends before the grant: RX0 at once, late grant ignored
  c.endTx(6000);
  CHECK(tr.take() == "RX0\r\n");
  CHECK(c.state() == frn::State::Idle);
  uint8_t grant[] = {1, 0, 1};
  tr.serverSend(grant, 3);
  spin(c, 6050);
  CHECK(tr.take().empty());
  CHECK(c.state() == frn::State::Idle);
  CHECK(li.txApproved == 0);
  CHECK(!c.txActive());
  spin(c, 9000);
  CHECK(tr.tx.find("TX0") == std::string::npos);
}

static void testLoginVariants() {
  printf("[login variants: NOK, missing AL, KP challenge, backup from server]\n");
  {
    MockTransport tr;
    MockListener li;
    frn::Client c(tr, li);
    c.setConfig(testConfig());
    c.start(0);
    spin(c, 1);
    tr.serverSend("2014003\r\n<AL>NOK</AL>\r\n");
    spin(c, 2);
    CHECK(c.state() == frn::State::LoginRejected);  // SvxLink would have accepted this
  }
  {
    MockTransport tr;
    MockListener li;
    frn::Client c(tr, li);
    c.setConfig(testConfig());
    c.start(0);
    spin(c, 1);
    tr.serverSend("2014003\r\n<SV>x</SV>\r\n");
    spin(c, 2);
    CHECK(c.state() == frn::State::LoginRejected);
  }
  {
    MockTransport tr;
    MockListener li;
    frn::Client c(tr, li);
    c.setConfig(testConfig());
    c.start(0);
    spin(c, 1);
    tr.take();
    tr.serverSend("2014003\r\n<KP>123456</KP><AL>ADMIN</AL><BN>bk.example.org</BN><BP>10025</BP>\r\n");
    spin(c, 2);
    CHECK(c.state() == frn::State::Idle);
    CHECK(tr.take() == "70204\r\nRX0\r\n");  // (12+2)*(34+1)+(56+4)*(56+7)=04270 -> 70204
    // link drops and the main server is down: the announced backup is tried
    tr.up = false;
    spin(c, 10);
    tr.connectOk = false;
    spin(c, 5100);
    CHECK(tr.lastHost == "server.voip-italia.net");
    spin(c, 5100 + 5100);
    CHECK(tr.lastHost == "bk.example.org" && tr.lastPort == 10025);
  }
  {
    // field sanitising
    MockTransport tr;
    MockListener li;
    frn::Client c(tr, li);
    frn::Config cfg = testConfig();
    cfg.description = " <b>Gateway</b>\r\nKV4P ";
    c.setConfig(cfg);
    c.start(0);
    spin(c, 1);
    CHECK(tr.take().find("<DS>(b)Gateway(/b)  KV4P</DS>") != std::string::npos);
  }
}

static void testRejectAndReconnect() {
  printf("[login rejected, timeouts, reconnect]\n");
  MockTransport tr;
  MockListener li;
  frn::Client c(tr, li);
  c.setConfig(testConfig());
  c.start(0);
  spin(c, 1);
  tr.serverSend("2014003\r\n<AL>WRONG</AL>\r\n");
  spin(c, 2);
  CHECK(c.state() == frn::State::LoginRejected);
  CHECK(!tr.up);
  spin(c, 200000);
  CHECK(tr.connects == 1);  // waits 5 minutes before retrying a rejected login
  spin(c, 300010);
  CHECK(tr.connects == 2);
  CHECK(c.state() == frn::State::LoggingIn1);

  // login timeout
  spin(c, 300010 + 15100);
  CHECK(c.state() == frn::State::Disconnected);
  spin(c, 300010 + 15100 + 5100);
  CHECK(tr.connects == 3);
  loginOk(tr, c, 321000);
  CHECK(c.state() == frn::State::Idle);

  // keepalives are sent, but the server stays silent: 30 s connection timeout
  spin(c, 321000 + 31000);
  CHECK(c.state() == frn::State::Disconnected);

  // connect failure -> backoff
  tr.connectOk = false;
  int before = tr.connects;
  spin(c, 400000);
  CHECK(tr.connects == before + 1);
  CHECK(c.state() == frn::State::Disconnected);
  spin(c, 400000 + 4000);
  CHECK(tr.connects == before + 1);
  spin(c, 400000 + 5100);
  CHECK(tr.connects == before + 2);  // 5 s, then 6 s, 7.2 s ...
  spin(c, 400000 + 5100 + 5900);
  CHECK(tr.connects == before + 2);
  spin(c, 400000 + 5100 + 6100);
  CHECK(tr.connects == before + 3);

  // remote closes mid-voice -> voice end reported
  tr.connectOk = true;
  spin(c, 500000);
  loginOk(tr, c, 500001);
  frn::Codec enc;
  int16_t pcm[frn::PACKET_SAMPLES];
  uint8_t pkt[frn::PACKET_BYTES];
  makeSine(pcm, frn::PACKET_SAMPLES, 500, 5000);
  enc.encodePacket(pcm, pkt);
  uint8_t head[] = {2, 0, 0};
  tr.serverSend(head, 3);
  tr.serverSend(pkt, sizeof(pkt));
  spin(c, 500100);
  CHECK(li.voiceStarts == 1);
  CHECK(li.voiceFrom == "?");  // unknown index
  tr.up = false;
  spin(c, 500200);
  CHECK(li.voiceEnds == 1);
  CHECK(c.state() == frn::State::Disconnected);
}

static void testBackup() {
  printf("[backup server]\n");
  MockTransport tr;
  MockListener li;
  frn::Client c(tr, li);
  frn::Config cfg = testConfig();
  cfg.backupServer = "backup.example.org";
  cfg.backupPort = 10025;
  c.setConfig(cfg);
  tr.connectOk = false;
  c.start(0);
  spin(c, 1);
  CHECK(tr.lastHost == "server.voip-italia.net");
  spin(c, 5100);
  CHECK(tr.lastHost == "backup.example.org" && tr.lastPort == 10025);
}

// ----------------------------------------------------------------- DSP
static double toneGain(double fIn, bool decim) {
  if (decim) {
    dsp::Decimator<6, 144> d;
    d.begin(48000, 3000);
    std::vector<float> out;
    for (int i = 0; i < 48000; i++) {
      float y;
      if (d.push(sinf(2 * M_PI * fIn * i / 48000.0f), &y)) out.push_back(y);
    }
    double e = 0;
    for (size_t i = 2000; i < out.size(); i++) e += out[i] * out[i];
    return sqrt(2 * e / (out.size() - 2000));
  } else {
    dsp::Interpolator<6, 144> it;
    it.begin(48000, 3000);
    std::vector<float> out;
    for (int i = 0; i < 8000; i++) {
      float y[6];
      it.push(sinf(2 * M_PI * fIn * i / 8000.0f), y);
      out.insert(out.end(), y, y + 6);
    }
    // measure only energy above 4 kHz (images) or total
    double e = 0;
    for (size_t i = 6000; i < out.size(); i++) e += out[i] * out[i];
    return sqrt(2 * e / (out.size() - 6000));
  }
}

static void testDsp() {
  printf("[resamplers]\n");
  double g300 = toneGain(300, true), g1k = toneGain(1000, true), g2500 = toneGain(2500, true);
  double g5k = toneGain(5000, true), g7k = toneGain(7000, true);
  printf("  decimator gain: 300Hz %.3f 1k %.3f 2.5k %.3f | alias 5k %.4f 7k %.4f\n", g300, g1k, g2500, g5k, g7k);
  CHECK(fabs(g300 - 1) < 0.02 && fabs(g1k - 1) < 0.02);
  CHECK(g2500 > 0.7);
  CHECK(g5k < 0.01 && g7k < 0.001);
  double i1k = toneGain(1000, false);
  printf("  interpolator gain 1k %.3f\n", i1k);
  CHECK(fabs(i1k - 1) < 0.03);

  dsp::FracResampler fr;
  fr.setStep(8040.0 / 8000.0);  // input 0.5 % fast
  int outN = 0;
  float o[2];
  for (int i = 0; i < 80400; i++) outN += fr.push((float)i, o);
  printf("  frac resampler: 80400 in -> %d out (expect ~80000)\n", outN);
  CHECK(abs(outN - 80000) < 5);
  fr.setStep(7960.0 / 8000.0);
  fr.reset();
  outN = 0;
  for (int i = 0; i < 79600; i++) outN += fr.push((float)i, o);
  CHECK(abs(outN - 80000) < 5);
}

static void testChain() {
  printf("[full audio chain: 48k ADC -> 8k -> GSM -> 8k -> 48k DAC]\n");
  dsp::DcBlocker dc;
  dc.begin(120, 48000);
  dsp::Decimator<6, 144> d;
  d.begin(48000, 3000);
  std::vector<int16_t> s8;
  for (int i = 0; i < 48000 * 2; i++) {
    float x = 2000 + 9000 * sinf(2 * M_PI * 800 * i / 48000.0f);  // with DC offset
    float y;
    if (d.push(dc.process(x), &y)) s8.push_back(dsp::clip16(y));
  }
  frn::Codec cod;
  std::vector<int16_t> rx;
  int16_t pcm[frn::PACKET_SAMPLES];
  uint8_t pkt[frn::PACKET_BYTES];
  for (size_t p = 0; p + frn::PACKET_SAMPLES <= s8.size(); p += frn::PACKET_SAMPLES) {
    cod.encodePacket(&s8[p], pkt);
    cod.decodePacket(pkt, pcm);
    rx.insert(rx.end(), pcm, pcm + frn::PACKET_SAMPLES);
  }
  dsp::Interpolator<6, 144> it;
  it.begin(48000, 3000);
  double e = 0, dcSum = 0;
  int n = 0;
  for (size_t i = 0; i < rx.size(); i++) {
    float y[6];
    it.push(rx[i], y);
    if (i > 4000)
      for (float v : y) {
        e += v * v;
        dcSum += v;
        n++;
      }
  }
  double rms = sqrt(e / n);
  printf("  output RMS %.0f (input tone RMS 6364), DC %.1f\n", rms, dcSum / n);
  CHECK(rms > 4500 && rms < 8000);
  CHECK(fabs(dcSum / n) < 200);
}

static void testCw() {
  printf("[CW id]\n");
  CwGenerator cw;
  cw.start("IZ0TST", 20, 800, 6000);
  std::vector<int16_t> out;
  int16_t b[160];
  size_t n;
  while ((n = cw.fill(b, 160)) > 0) out.insert(out.end(), b, b + n);
  CHECK(cw.done());
  // PARIS standard: dot = 60 ms @ 20 WPM. "IZ0TST" = 76 units incl. gaps (approx)
  double secs = out.size() / 8000.0;
  printf("  'IZ0TST' @20 WPM: %.2f s\n", secs);
  CHECK(secs > 3.5 && secs < 6.0);
  int16_t peak = 0;
  for (auto v : out) peak = v > peak ? v : peak;
  CHECK(peak > 5500 && peak <= 6000);
}

int main() {
  testLogin();
  testListsAndKeepalive();
  testVoiceRx();
  testTx();
  testTxDeniedAndLateGrant();
  testLoginVariants();
  testRejectAndReconnect();
  testBackup();
  testDsp();
  testChain();
  testCw();
  printf("\n%d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
