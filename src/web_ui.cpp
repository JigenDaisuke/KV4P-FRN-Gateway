// Web UI back end: static page from flash + small JSON API.
// HTTP basic auth on everything: user "admin", password = settings.webPass.
#include "web_ui.h"

#include <Update.h>
#include <WebServer.h>
#include <WiFi.h>

#include "logbuf.h"
#include "tones.h"
#include "version.h"
#include "web_logo.h"
#include "web_page.h"


namespace {
WebServer server(80);
Settings* S = nullptr;
Gateway* G = nullptr;
uint32_t rebootAt = 0;
bool otaOk = false;

bool auth() {
  if (S->webPass.length() == 0) return true;
  if (server.authenticate("admin", S->webPass.c_str())) return true;
  server.requestAuthentication(BASIC_AUTH, "FRN ITALIA Gateway");
  return false;
}

// --- tiny JSON writer --------------------------------------------------------
struct Json {
  String s;
  bool first = true;
  Json() {
    s.reserve(1800);
    s = "{";
  }
  void key(const char* k) {
    if (!first) s += ',';
    first = false;
    s += '"';
    s += k;
    s += "\":";
  }
  void str(const char* k, const String& v) {
    key(k);
    s += '"';
    for (size_t i = 0; i < v.length(); i++) {
      char c = v[i];
      if (c == '"' || c == '\\') {
        s += '\\';
        s += c;
      } else if ((unsigned char)c < 0x20) {
        s += ' ';
      } else {
        s += c;
      }
    }
    s += '"';
  }
  void num(const char* k, long v) {
    char b[16];
    snprintf(b, sizeof(b), "%ld", v);
    key(k);
    s += b;
  }
  void flt(const char* k, float v, int dec) {
    char b[24];
    snprintf(b, sizeof(b), "%.*f", dec, (double)v);
    key(k);
    s += b;
  }
  void boolean(const char* k, bool v) {
    key(k);
    s += v ? "true" : "false";
  }
  const String& end() {
    s += '}';
    return s;
  }
};

void handleRoot() {
  if (!auth()) return;
  server.send_P(200, "text/html", INDEX_HTML);
}

void handleLogo() {
  server.sendHeader("Cache-Control", "max-age=86400");
  server.send_P(200, "image/png", (const char*)LOGO_PNG, sizeof(LOGO_PNG));
}

void handleStatus() {
  if (!auth()) return;
  GatewayStatus st;
  G->getStatus(st);
  Json j;
  j.str("gw", st.gw);
  j.str("frn", st.frn);
  j.str("talker", st.talker);
  j.str("lastLocal", st.lastLocal);
  j.boolean("ptt", st.ptt);
  j.boolean("carrier", st.carrier);
  j.boolean("radio", st.radioOk);
  j.boolean("wifi", st.wifi);
  j.str("ssid", st.wifi ? WiFi.SSID() : String(""));
  j.num("rssi", st.rssi);
  j.str("ip", WiFi.localIP().toString());
  j.num("level", st.rxLevel);
  j.num("adc", st.adcRate);
  j.num("clients", st.clients);
  j.str("net", S->frnNet);
  j.num("rx", (long)st.voiceRx);
  j.num("tx", (long)st.voiceTx);
  j.num("logins", (long)st.logins);
  j.num("conn", (long)st.reconnects);
  j.num("bad", (long)st.badFrames);
  j.num("up", (long)(millis() / 60000));
  j.num("heap", (long)ESP.getFreeHeap());
  server.send(200, "application/json", j.end());
}

void handleConfig() {
  if (!auth()) return;
  const Settings& s = *S;
  Json j;
  j.str("version", FW_VERSION);
  for (int i = 0; i < WIFI_SLOTS; i++) {
    char k[16];
    snprintf(k, sizeof(k), "wifiSsid%d", i);
    j.str(k, s.wifiSsid[i]);
    snprintf(k, sizeof(k), "wifiPass%d_set", i);
    j.boolean(k, s.wifiPass[i].length() > 0);
  }
  j.str("hostname", s.hostname);
  j.boolean("webPass_set", s.webPass.length() > 0);
  j.str("frnServer", s.frnServer);
  j.num("frnPort", s.frnPort);
  j.str("frnBkServer", s.frnBackupServer);
  j.num("frnBkPort", s.frnBackupPort);
  j.str("frnNet", s.frnNet);
  j.str("frnVersion", s.frnVersion);
  j.str("frnEmail", s.frnEmail);
  j.boolean("frnPassword_set", s.frnPassword.length() > 0);
  j.str("frnCallsign", s.frnCallsign);
  j.str("frnName", s.frnName);
  j.str("frnDescr", s.frnDescription);
  j.str("frnCountry", s.frnCountry);
  j.str("frnCity", s.frnCity);
  j.str("frnBC", s.frnBandChannel);
  j.num("frnCl", s.frnClientType);
  Settings tmp = s;
  tmp.frnBandChannel = "";
  tmp.frnClientType = 1;
  j.str("bcAuto", settings::bandChannel(tmp));
  j.num("uhf", s.uhf ? 1 : 0);
  j.flt("freqRx", s.freqRx, 5);
  j.flt("freqTx", s.freqTx, 5);
  j.num("bw", s.bandwidth);
  j.num("ctcssTx", s.ctcssTx);
  j.num("ctcssRx", s.ctcssRx);
  j.num("squelch", s.squelch);
  j.num("volume", s.volume);
  j.boolean("fEmph", s.filterEmphasis);
  j.boolean("fHp", s.filterHighpass);
  j.boolean("fLp", s.filterLowpass);
  j.num("hiPow", s.highPower ? 1 : 0);
  j.num("powPin", s.powerPinMode);
  j.num("sqMode", s.sqMode);
  j.num("voxThr", s.voxThreshold);
  j.num("sqOpenMs", s.sqOpenMs);
  j.num("sqHangMs", s.sqHangMs);
  j.num("txDelayMs", s.txDelayMs);
  j.num("txTailMs", s.txTailMs);
  j.num("rxLockMs", s.rxLockoutMs);
  j.num("jitterMs", s.jitterMs);
  j.num("totSec", s.totSec);
  j.flt("rxGain", s.rxGain, 2);
  j.flt("txGain", s.txGain, 2);
  j.num("tQso", s.toneQsoEnd);
  j.num("tQsoHz", s.toneQsoEndHz);
  j.num("tLoc", s.toneLocal);
  j.num("tLocHz", s.toneLocalHz);
  j.num("tBusy", s.toneBusy);
  j.num("tBusyHz", s.toneBusyHz);
  j.num("tLevel", s.toneLevel);
  j.boolean("cwId", s.cwIdEnabled);
  j.num("cwIdMin", s.cwIdMinutes);
  j.num("cwWpm", s.cwWpm);
  j.num("cwTone", s.cwToneHz);
  j.num("cwLevel", s.cwLevel);
  server.send(200, "application/json", j.end());
}

void handleLog() {
  if (!auth()) return;
  String out;
  out.reserve(4000);
  logbuf::dump(out);
  server.send(200, "text/plain", out);
}

String arg(const char* n, const String& def) { return server.hasArg(n) ? server.arg(n) : def; }
long argL(const char* n, long def) { return server.hasArg(n) && server.arg(n).length() ? server.arg(n).toInt() : def; }
float argF(const char* n, float def) {
  return server.hasArg(n) && server.arg(n).length() ? server.arg(n).toFloat() : def;
}
template <typename T>
T clampT(long v, long lo, long hi) {
  return (T)(v < lo ? lo : (v > hi ? hi : v));
}

void handleSave() {
  if (!auth()) return;
  Settings& s = *S;
  for (int i = 0; i < WIFI_SLOTS; i++) {
    char ks[16], kp[16];
    snprintf(ks, sizeof(ks), "wifiSsid%d", i);
    snprintf(kp, sizeof(kp), "wifiPass%d", i);
    if (!server.hasArg(ks)) continue;
    String ssid = server.arg(ks);
    ssid.trim();
    String pass = arg(kp, "");
    if (ssid != s.wifiSsid[i]) s.wifiPass[i] = pass;  // new network: password as typed (open = empty)
    else if (pass.length()) s.wifiPass[i] = pass;     // same network: empty field = keep
    s.wifiSsid[i] = ssid;
    if (!ssid.length()) s.wifiPass[i] = "";
  }
  s.hostname = arg("hostname", s.hostname);
  if (arg("webPass", "").length()) s.webPass = arg("webPass", "");

  s.frnServer = arg("frnServer", s.frnServer);
  s.frnPort = clampT<uint16_t>(argL("frnPort", s.frnPort), 1, 65535);
  s.frnBackupServer = arg("frnBkServer", s.frnBackupServer);
  s.frnBackupPort = clampT<uint16_t>(argL("frnBkPort", s.frnBackupPort), 0, 65535);
  s.frnNet = arg("frnNet", s.frnNet);
  s.frnVersion = arg("frnVersion", s.frnVersion);
  s.frnEmail = arg("frnEmail", s.frnEmail);
  if (arg("frnPassword", "").length()) s.frnPassword = arg("frnPassword", "");
  s.frnCallsign = arg("frnCallsign", s.frnCallsign);
  s.frnCallsign.toUpperCase();
  s.frnName = arg("frnName", s.frnName);
  s.frnDescription = arg("frnDescr", s.frnDescription);
  s.frnCountry = arg("frnCountry", s.frnCountry);
  s.frnCity = arg("frnCity", s.frnCity);
  s.frnBandChannel = arg("frnBC", s.frnBandChannel);
  s.frnClientType = clampT<uint8_t>(argL("frnCl", s.frnClientType), 0, 2);

  s.uhf = argL("uhf", s.uhf) != 0;
  s.freqRx = argF("freqRx", s.freqRx);
  s.freqTx = argF("freqTx", s.freqTx);
  s.bandwidth = clampT<uint8_t>(argL("bw", s.bandwidth), 0, 1);
  s.ctcssTx = clampT<uint8_t>(argL("ctcssTx", s.ctcssTx), 0, 38);
  s.ctcssRx = clampT<uint8_t>(argL("ctcssRx", s.ctcssRx), 0, 38);
  s.squelch = clampT<uint8_t>(argL("squelch", s.squelch), 1, 8);
  s.volume = clampT<uint8_t>(argL("volume", s.volume), 1, 8);
  s.filterEmphasis = server.hasArg("fEmph");
  s.filterHighpass = server.hasArg("fHp");
  s.filterLowpass = server.hasArg("fLp");
  s.highPower = argL("hiPow", s.highPower) != 0;
  s.powerPinMode = clampT<uint8_t>(argL("powPin", s.powerPinMode), 0, 2);

  s.sqMode = clampT<uint8_t>(argL("sqMode", s.sqMode), 0, 2);
  s.voxThreshold = clampT<uint16_t>(argL("voxThr", s.voxThreshold), 10, 30000);
  s.sqOpenMs = clampT<uint16_t>(argL("sqOpenMs", s.sqOpenMs), 0, 2000);
  s.sqHangMs = clampT<uint16_t>(argL("sqHangMs", s.sqHangMs), 50, 5000);
  s.txDelayMs = clampT<uint16_t>(argL("txDelayMs", s.txDelayMs), 0, 3000);
  s.txTailMs = clampT<uint16_t>(argL("txTailMs", s.txTailMs), 0, 3000);
  s.rxLockoutMs = clampT<uint16_t>(argL("rxLockMs", s.rxLockoutMs), 0, 5000);
  s.jitterMs = clampT<uint16_t>(argL("jitterMs", s.jitterMs), 0, 1500);
  s.totSec = clampT<uint16_t>(argL("totSec", s.totSec), 10, 900);
  s.rxGain = constrain(argF("rxGain", s.rxGain), 0.05f, 20.0f);
  s.txGain = constrain(argF("txGain", s.txGain), 0.05f, 20.0f);

  s.toneQsoEnd = clampT<uint8_t>(argL("tQso", s.toneQsoEnd), 0, tones::PRESETS - 1);
  s.toneQsoEndHz = clampT<uint16_t>(argL("tQsoHz", s.toneQsoEndHz), 300, 3000);
  s.toneLocal = clampT<uint8_t>(argL("tLoc", s.toneLocal), 0, tones::PRESETS - 1);
  s.toneLocalHz = clampT<uint16_t>(argL("tLocHz", s.toneLocalHz), 300, 3000);
  s.toneBusy = clampT<uint8_t>(argL("tBusy", s.toneBusy), 0, tones::PRESETS - 1);
  s.toneBusyHz = clampT<uint16_t>(argL("tBusyHz", s.toneBusyHz), 300, 3000);
  s.toneLevel = clampT<uint16_t>(argL("tLevel", s.toneLevel), 500, 30000);

  s.cwIdEnabled = server.hasArg("cwId");
  s.cwIdMinutes = clampT<uint8_t>(argL("cwIdMin", s.cwIdMinutes), 1, 60);
  s.cwWpm = clampT<uint8_t>(argL("cwWpm", s.cwWpm), 5, 40);
  s.cwToneHz = clampT<uint16_t>(argL("cwTone", s.cwToneHz), 300, 2000);
  s.cwLevel = clampT<uint16_t>(argL("cwLevel", s.cwLevel), 500, 30000);

  // Frequency sanity for the selected module
  float lo = s.uhf ? 400.0f : 134.0f, hi = s.uhf ? 480.0f : 174.0f;
  s.freqRx = constrain(s.freqRx, lo, hi);
  s.freqTx = constrain(s.freqTx, lo, hi);

  settings::save(s);
  LOGF("settings saved from web, rebooting");
  server.send(200, "application/json", "{\"ok\":true}");
  rebootAt = millis() + 1000;
}

void handleTest() {
  if (!auth()) return;
  int t = (int)argL("t", 0);
  if (t >= 1 && t <= 3) G->requestTestTone(t);
  server.send(200, "application/json", "{\"ok\":true}");
}

void handleUpdateDone() {
  if (!auth()) return;
  server.send(otaOk ? 200 : 500, "text/plain", otaOk ? "OK" : "FAILED");
  if (otaOk) rebootAt = millis() + 1000;
}

void handleUpload() {
  HTTPUpload& up = server.upload();
  if (up.status == UPLOAD_FILE_START) {
    otaOk = false;
    if (S->webPass.length() && !server.authenticate("admin", S->webPass.c_str())) return;
    LOGF("OTA: receiving %s", up.filename.c_str());
    Update.begin(UPDATE_SIZE_UNKNOWN);
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (Update.isRunning()) Update.write(up.buf, up.currentSize);
  } else if (up.status == UPLOAD_FILE_END) {
    if (Update.isRunning()) {
      otaOk = Update.end(true);
      if (otaOk) LOGF("OTA: %u bytes OK", (unsigned)up.totalSize);
      else LOGF("OTA: error %s", Update.errorString());
    }
  }
}
}  // namespace

namespace webui {

void begin(Settings& s, Gateway& gw) {
  S = &s;
  G = &gw;
  server.on("/", HTTP_GET, handleRoot);
  server.on("/logo.png", HTTP_GET, handleLogo);
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/config", HTTP_GET, handleConfig);
  server.on("/api/test", HTTP_POST, handleTest);
  server.on("/log", HTTP_GET, handleLog);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/update", HTTP_POST, handleUpdateDone, handleUpload);
  server.on("/reboot", HTTP_POST, []() {
    if (!auth()) return;
    server.send(200, "application/json", "{\"ok\":true}");
    rebootAt = millis() + 500;
  });
  server.onNotFound([]() { server.send(404, "text/plain", "not found"); });
  server.begin();
}

void loop() {
  server.handleClient();
  if (rebootAt && (int32_t)(millis() - rebootAt) >= 0) {
    G->requestShutdown();
    uint32_t t0 = millis();
    while (!G->shutdownDone() && millis() - t0 < 2000) delay(10);
    delay(300);  // let the TCP close reach the server
    ESP.restart();
  }
}

}  // namespace webui
