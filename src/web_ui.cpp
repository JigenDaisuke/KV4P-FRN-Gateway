// Configuration / status web page (Italian UI). HTTP basic auth: admin / webPass.
#include "web_ui.h"

#include <Update.h>
#include <WebServer.h>
#include <WiFi.h>

#include "logbuf.h"

namespace {
WebServer server(80);
Settings* S = nullptr;
Gateway* G = nullptr;
uint32_t rebootAt = 0;
bool otaOk = false;

String esc(const String& in) {
  String o;
  o.reserve(in.length() + 8);
  for (size_t i = 0; i < in.length(); i++) {
    char c = in[i];
    if (c == '&') o += "&amp;";
    else if (c == '<') o += "&lt;";
    else if (c == '>') o += "&gt;";
    else if (c == '"') o += "&quot;";
    else o += c;
  }
  return o;
}

bool auth() {
  if (S->webPass.length() == 0) return true;
  if (server.authenticate("admin", S->webPass.c_str())) return true;
  server.requestAuthentication(BASIC_AUTH, "FRN Gateway");
  return false;
}

void text(String& h, const char* label, const char* name, const String& v, const char* hint = "") {
  h += "<label>";
  h += label;
  h += "<input name='";
  h += name;
  h += "' value='" + esc(v) + "'>";
  if (*hint) {
    h += "<small>";
    h += hint;
    h += "</small>";
  }
  h += "</label>";
}
void secret(String& h, const char* label, const char* name, bool isSet) {
  h += "<label>";
  h += label;
  h += "<input type='password' name='";
  h += name;
  h += "' placeholder='";
  h += isSet ? "(impostata - lascia vuoto per non cambiarla)" : "(non impostata)";
  h += "'></label>";
}
void num(String& h, const char* label, const char* name, double v, const char* step, const char* hint = "") {
  char b[32];
  snprintf(b, sizeof(b), (strchr(step, '.') ? "%.4f" : "%.0f"), v);
  h += "<label>";
  h += label;
  h += "<input type='number' step='";
  h += step;
  h += "' name='";
  h += name;
  h += "' value='";
  h += b;
  h += "'>";
  if (*hint) {
    h += "<small>";
    h += hint;
    h += "</small>";
  }
  h += "</label>";
}
void check(String& h, const char* label, const char* name, bool v) {
  h += "<label class='ck'><input type='checkbox' name='";
  h += name;
  h += "' value='1'";
  if (v) h += " checked";
  h += "> ";
  h += label;
  h += "</label>";
}
void ctcssSelect(String& h, const char* label, const char* name, uint8_t v) {
  h += "<label>";
  h += label;
  h += "<select name='";
  h += name;
  h += "'>";
  for (int i = 0; i <= 38; i++) {
    char b[48];
    if (i == 0) snprintf(b, sizeof(b), "<option value='0'%s>nessuno</option>", v == 0 ? " selected" : "");
    else
      snprintf(b, sizeof(b), "<option value='%d'%s>%.1f Hz</option>", i, v == i ? " selected" : "",
               (double)settings::ctcssHz(i));
    h += b;
  }
  h += "</select></label>";
}

const char CSS[] PROGMEM =
    "body{font-family:system-ui,sans-serif;max-width:860px;margin:auto;padding:12px;background:#f4f5f7;color:#222}"
    "h1{font-size:1.4em}h2{font-size:1.1em;margin:18px 0 6px;border-bottom:1px solid #ccc}"
    "fieldset{border:0;display:grid;grid-template-columns:repeat(auto-fill,minmax(250px,1fr));gap:10px}"
    "label{display:flex;flex-direction:column;font-size:.85em}label.ck{flex-direction:row;align-items:center;gap:6px}"
    "input,select{font-size:1em;padding:5px;margin-top:2px}small{color:#666}"
    "#st{display:grid;grid-template-columns:repeat(auto-fill,minmax(190px,1fr));gap:6px;background:#fff;padding:10px;border-radius:8px}"
    "#st div{font-size:.9em}#st b{display:block;font-size:1.05em}"
    "button{font-size:1em;padding:8px 18px;margin:12px 6px 0 0}pre{background:#111;color:#9f9;padding:8px;"
    "max-height:300px;overflow:auto;font-size:.75em}.on{color:#c00;font-weight:bold}";

const char JS[] PROGMEM =
    "function u(){fetch('/status').then(r=>r.json()).then(j=>{let h='';for(const k in j){let v=j[k];"
    "if(v===true)v='<span class=on>SI</span>';if(v===false)v='no';h+='<div>'+k+'<b>'+v+'</b></div>'}"
    "document.getElementById('st').innerHTML=h});fetch('/log').then(r=>r.text()).then(t=>{let p=document."
    "getElementById('lg');p.textContent=t;p.scrollTop=p.scrollHeight})}setInterval(u,1500);u();";

void handleRoot() {
  if (!auth()) return;
  const Settings& s = *S;
  String h;
  h.reserve(14000);
  h += "<!DOCTYPE html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width'>"
       "<title>FRN ITALIA Gateway</title><style>";
  h += FPSTR(CSS);
  h += "</style></head><body><h1>FRN ITALIA &mdash; Gateway KV4P</h1><p><a href='https://t.me/frnitalia' target='_blank'>Gruppo Telegram FRN ITALIA</a></p><div id='st'>caricamento...</div>"
       "<h2>Log</h2><pre id='lg'></pre><form method='POST' action='/save'>";

  h += "<h2>Wi-Fi</h2><fieldset>";
  text(h, "SSID", "wifiSsid", s.wifiSsid);
  secret(h, "Password Wi-Fi", "wifiPass", s.wifiPass.length() > 0);
  text(h, "Nome host (mDNS)", "hostname", s.hostname);
  secret(h, "Password pagina web (utente admin)", "webPass", s.webPass.length() > 0);
  h += "</fieldset>";

  h += "<h2>Server FRN</h2><fieldset>";
  text(h, "Server", "frnServer", s.frnServer);
  num(h, "Porta", "frnPort", s.frnPort, "1");
  text(h, "Server di backup", "frnBkServer", s.frnBackupServer, "vuoto = nessuno");
  num(h, "Porta backup", "frnBkPort", s.frnBackupPort, "1");
  text(h, "Stanza (net)", "frnNet", s.frnNet);
  text(h, "Versione client", "frnVersion", s.frnVersion, "es. 2014003");
  text(h, "E-mail di registrazione", "frnEmail", s.frnEmail);
  secret(h, "Password FRN", "frnPassword", s.frnPassword.length() > 0);
  text(h, "Nominativo", "frnCallsign", s.frnCallsign, "usato anche per l'ID in CW");
  text(h, "Nome operatore", "frnName", s.frnName);
  text(h, "Descrizione", "frnDescr", s.frnDescription);
  text(h, "Nazione", "frnCountry", s.frnCountry);
  text(h, "Citta - zona", "frnCity", s.frnCity, "es. Roma - Centro");
  String bcHint = "vuoto = automatico: " + esc(settings::bandChannel(s));
  text(h, "Banda/canale (BC)", "frnBC", s.frnBandChannel, bcHint.c_str());
  h += "</fieldset>";

  h += "<h2>Radio SA818</h2><fieldset>";
  h += "<label>Modulo<select name='uhf'><option value='0'";
  if (!s.uhf) h += " selected";
  h += ">VHF 134-174 MHz</option><option value='1'";
  if (s.uhf) h += " selected";
  h += ">UHF 400-480 MHz</option></select></label>";
  num(h, "Frequenza RX (MHz)", "freqRx", s.freqRx, "0.0125");
  num(h, "Frequenza TX (MHz)", "freqTx", s.freqTx, "0.0125");
  h += "<label>Canalizzazione<select name='bw'><option value='0'";
  if (s.bandwidth == 0) h += " selected";
  h += ">12,5 kHz</option><option value='1'";
  if (s.bandwidth == 1) h += " selected";
  h += ">25 kHz</option></select></label>";
  ctcssSelect(h, "CTCSS in TX", "ctcssTx", s.ctcssTx);
  ctcssSelect(h, "CTCSS in RX (tono di accesso)", "ctcssRx", s.ctcssRx);
  num(h, "Squelch (1-8)", "squelch", s.squelch, "1");
  num(h, "Volume uscita modulo (1-8)", "volume", s.volume, "1");
  check(h, "Enfasi/de-enfasi", "fEmph", s.filterEmphasis);
  check(h, "Filtro passa-alto 300 Hz", "fHp", s.filterHighpass);
  check(h, "Filtro passa-basso 3 kHz", "fLp", s.filterLowpass);
  h += "</fieldset>";

  h += "<h2>Comportamento gateway</h2><fieldset>";
  h += "<label>Rilevamento segnale<select name='sqMode'>";
  const char* modes[] = {"Pin SQ del modulo", "Livello audio (VOX)", "Entrambi"};
  for (int i = 0; i < 3; i++) {
    h += "<option value='" + String(i) + "'";
    if (s.sqMode == i) h += " selected";
    h += ">";
    h += modes[i];
    h += "</option>";
  }
  h += "</select></label>";
  num(h, "Soglia VOX (RMS)", "voxThr", s.voxThreshold, "1", "guarda 'livello RX' nello stato");
  num(h, "Ritardo apertura squelch (ms)", "sqOpenMs", s.sqOpenMs, "1");
  num(h, "Coda squelch (ms)", "sqHangMs", s.sqHangMs, "1");
  num(h, "Ritardo PTT &rarr; audio (ms)", "txDelayMs", s.txDelayMs, "1");
  num(h, "Coda dopo l'audio (ms)", "txTailMs", s.txTailMs, "1");
  num(h, "Blocco RX dopo TX (ms)", "rxLockMs", s.rxLockoutMs, "1");
  num(h, "Buffer anti-jitter (ms)", "jitterMs", s.jitterMs, "1");
  num(h, "Time-out (s)", "totSec", s.totSec, "1");
  num(h, "Guadagno radio &rarr; FRN", "rxGain", s.rxGain, "0.05");
  num(h, "Guadagno FRN &rarr; radio", "txGain", s.txGain, "0.05");
  h += "</fieldset>";

  h += "<h2>Identificativo CW</h2><fieldset>";
  check(h, "Invia il nominativo in CW", "cwId", s.cwIdEnabled);
  num(h, "Al massimo ogni (minuti)", "cwIdMin", s.cwIdMinutes, "1");
  num(h, "Velocita (WPM)", "cwWpm", s.cwWpm, "1");
  num(h, "Nota (Hz)", "cwTone", s.cwToneHz, "1");
  num(h, "Livello", "cwLevel", s.cwLevel, "1");
  h += "</fieldset>";

  h += "<button type='submit'>Salva e riavvia</button></form>"
       "<form method='POST' action='/update' enctype='multipart/form-data'><h2>Aggiornamento firmware</h2>"
       "<input type='file' name='fw' accept='.bin'><button>Carica firmware.bin</button></form>"
       "<form method='POST' action='/reboot'><button>Riavvia</button></form><script>";
  h += FPSTR(JS);
  h += "</script></body></html>";
  server.send(200, "text/html", h);
}

void jsonSafe(char* p) {
  for (; *p; p++)
    if (*p == '"' || *p == '\\' || (unsigned char)*p < 0x20) *p = '\'';
}

void handleStatus() {
  if (!auth()) return;
  GatewayStatus st;
  G->getStatus(st);
  jsonSafe(st.talker);
  jsonSafe(st.lastLocal);
  char b[900];
  snprintf(b, sizeof(b),
           "{\"gateway\":\"%s\",\"FRN\":\"%s\",\"PTT (TX)\":%s,\"segnale RX\":%s,\"livello RX\":%d,"
           "\"ultimo da FRN\":\"%s\",\"ultimo da radio\":\"%s\",\"stazioni in rete\":%d,"
           "\"Wi-Fi\":\"%s\",\"IP\":\"%s\",\"modulo radio\":\"%s\",\"clock ADC (Hz)\":%d,"
           "\"pacchetti RX/TX\":\"%lu / %lu\",\"login / connessioni\":\"%lu / %lu\",\"frame GSM errati\":%lu,"
           "\"uptime (min)\":%lu,\"heap libero\":%lu}",
           st.gw, st.frn, st.ptt ? "true" : "false", st.carrier ? "true" : "false", st.rxLevel, st.talker,
           st.lastLocal, st.clients, st.wifi ? (String(st.rssi) + " dBm").c_str() : "non connesso",
           WiFi.localIP().toString().c_str(), st.radioOk ? "OK" : "NON TROVATO", st.adcRate,
           (unsigned long)st.voiceRx, (unsigned long)st.voiceTx, (unsigned long)st.logins,
           (unsigned long)st.reconnects, (unsigned long)st.badFrames, (unsigned long)(millis() / 60000),
           (unsigned long)ESP.getFreeHeap());
  server.send(200, "application/json", b);
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
  s.wifiSsid = arg("wifiSsid", s.wifiSsid);
  if (arg("wifiPass", "").length()) s.wifiPass = arg("wifiPass", "");
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
  server.send(200, "text/html",
              "<meta charset='utf-8'><meta http-equiv='refresh' content='8;url=/'>Salvato. Riavvio in corso...");
  rebootAt = millis() + 1000;
}

void handleUpdateDone() {
  if (!auth()) return;
  bool ok = otaOk;
  server.send(200, "text/html",
              ok ? "<meta charset='utf-8'><meta http-equiv='refresh' content='15;url=/'>Aggiornamento OK, riavvio..."
                 : "<meta charset='utf-8'>Aggiornamento FALLITO");
  if (ok) rebootAt = millis() + 1000;
}

void handleUpload() {
  HTTPUpload& up = server.upload();
  if (up.status == UPLOAD_FILE_START) {
    if (S->webPass.length() && !server.authenticate("admin", S->webPass.c_str())) return;
    LOGF("OTA: receiving %s", up.filename.c_str());
    otaOk = false;
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
  server.on("/status", HTTP_GET, handleStatus);
  server.on("/log", HTTP_GET, handleLog);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/update", HTTP_POST, handleUpdateDone, handleUpload);
  server.on("/reboot", HTTP_POST, []() {
    if (!auth()) return;
    server.send(200, "text/html", "<meta http-equiv='refresh' content='8;url=/'>Riavvio...");
    rebootAt = millis() + 500;
  });
  server.onNotFound([]() { server.send(404, "text/plain", "not found"); });
  server.begin();
}

void loop() {
  server.handleClient();
  if (rebootAt && (int32_t)(millis() - rebootAt) >= 0) ESP.restart();
}

}  // namespace webui
