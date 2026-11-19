// KV4P HT (ESP32 + SA818) stand-alone Free Radio Network gateway.
//
// Boot sequence: settings -> SA818 -> Wi-Fi (or config access point) ->
// web page -> gateway tasks. See README.md for details.
#include <Arduino.h>
#include <ESPmDNS.h>
#include <WiFi.h>

#include "board_kv4p.h"
#include "gateway.h"
#include "logbuf.h"
#include "radio_sa818.h"
#include "settings.h"
#include "version.h"
#include "web_ui.h"


static Settings cfg;
static RadioSA818 radio;
static Gateway gateway;
static bool apActive = false;
static uint32_t staConnectedSince = 0;

// AlterFRN closes the connection during login if callsign, name, city or
// country are empty (seen with the reference client): don't even try.
static bool frnProfileComplete() {
  return cfg.frnEmail.length() && cfg.frnPassword.length() && cfg.frnCallsign.length() && cfg.frnName.length() &&
         cfg.frnCity.length() && cfg.frnCountry.length();
}

static void checkFactoryReset() {
  pinMode(board::PIN_BOOT_BTN, INPUT_PULLUP);
  if (digitalRead(board::PIN_BOOT_BTN) != LOW) return;
  uint32_t t0 = millis();
  while (digitalRead(board::PIN_BOOT_BTN) == LOW) {
    digitalWrite(board::PIN_LED, (millis() / 100) & 1);
    if (millis() - t0 > 5000) {
      settings::factoryReset();
      LOGF("FACTORY RESET done");
      for (int i = 0; i < 20; i++) {
        digitalWrite(board::PIN_LED, i & 1);
        delay(50);
      }
      ESP.restart();
    }
    delay(10);
  }
}

static void startAccessPoint() {
  uint8_t mac[6];
  WiFi.macAddress(mac);
  char ssid[24];
  snprintf(ssid, sizeof(ssid), "FRN-GW-%02X%02X", mac[4], mac[5]);
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(ssid, "frngateway");
  apActive = true;
  LOGF("Config access point '%s' (password frngateway) -> http://%s/", ssid,
       WiFi.softAPIP().toString().c_str());
}

// Scan and connect to the first configured network (in priority order) that
// is on the air. With `blind`, networks not seen in the scan (hidden SSIDs)
// are also tried at the end. Blocking: used at boot and on long outages.
static bool connectFirstAvailable(uint32_t waitMs, bool blind) {
  if (!settings::anyWifi(cfg)) return false;
  int n = WiFi.scanNetworks();
  bool seen[WIFI_SLOTS] = {false, false, false};
  for (int slot = 0; slot < WIFI_SLOTS; slot++)
    for (int i = 0; i < n && cfg.wifiSsid[slot].length(); i++)
      if (WiFi.SSID(i) == cfg.wifiSsid[slot]) seen[slot] = true;
  WiFi.scanDelete();
  for (int pass = 0; pass < (blind ? 2 : 1); pass++) {
    for (int slot = 0; slot < WIFI_SLOTS; slot++) {
      const String& ssid = cfg.wifiSsid[slot];
      if (!ssid.length() || seen[slot] != (pass == 0)) continue;
      LOGF("Wi-Fi: connecting to '%s' (network %d%s)", ssid.c_str(), slot + 1, pass ? ", not seen in scan" : "");
      WiFi.begin(ssid.c_str(), cfg.wifiPass[slot].c_str());
      uint32_t t0 = millis();
      while (WiFi.status() != WL_CONNECTED && millis() - t0 < waitMs) {
        digitalWrite(board::PIN_LED, (millis() / 250) & 1);
        webui::loop();  // keep the config page responsive
        delay(50);
      }
      if (WiFi.status() == WL_CONNECTED) return true;
      LOGF("Wi-Fi: '%s' did not connect (password?)", ssid.c_str());
      WiFi.disconnect();
    }
  }
  return false;
}

void setup() {
  Serial.begin(115200);
  delay(200);
  pinMode(board::PIN_LED, OUTPUT);
  logbuf::begin();
  LOGF("KV4P FRN gateway v%s", FW_VERSION);

  checkFactoryReset();
  settings::load(cfg);

  if (radio.begin(Serial2)) {
    LOGF("SA818 found");
    if (radio.configure(cfg))
      LOGF("SA818 set: RX %.4f TX %.4f MHz, CTCSS tx %u rx %u, SQ %u", (double)cfg.freqRx, (double)cfg.freqTx,
           cfg.ctcssTx, cfg.ctcssRx, cfg.squelch);
    else
      LOGF("SA818 configuration error (last reply '%s') - check frequency vs VHF/UHF module",
           radio.lastReply().c_str());
    if (radio.applyPower(cfg))
      LOGF("TX power: %s", cfg.highPower ? "HIGH (~1 W)" : "LOW (~0.5 W)");
    else
      LOGF("TX power: fixed by hardware (H/L pin not wired, module at high power)");
  } else {
    LOGF("SA818 NOT responding - check module / power");
  }

  WiFi.persistent(false);
  WiFi.setHostname(cfg.hostname.c_str());
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);  // modem sleep adds latency and jitter to the audio stream
  WiFi.setAutoReconnect(true);
  webui::begin(cfg, gateway);  // before connecting, so the page answers as soon as possible
  if (connectFirstAvailable(15000, true)) {
    LOGF("Wi-Fi OK: '%s' %s  RSSI %d dBm  -> http://%s/ or http://%s.local/", WiFi.SSID().c_str(),
         WiFi.localIP().toString().c_str(), WiFi.RSSI(), WiFi.localIP().toString().c_str(), cfg.hostname.c_str());
    staConnectedSince = millis();
  } else {
    startAccessPoint();
  }
  if (MDNS.begin(cfg.hostname.c_str())) MDNS.addService("http", "tcp", 80);

  // Local time (EchoLink directory shows it); SNTP keeps retrying until online.
  configTzTime("CET-1CEST,M3.5.0,M10.5.0/3", "pool.ntp.org", "time.google.com");
  gateway.begin(cfg, radio);
  if (!frnProfileComplete())
    LOGF("FRN profile incomplete: fill in e-mail, password, callsign, name, city and country on the web page");
  if (cfg.elCallsign.length() == 0)
    LOGF("EchoLink not configured (scheda EchoLink della pagina web)");
}

void loop() {
  webui::loop();

  static bool wasUp = false;
  static uint32_t downSince = 0, lastAttempt = 0;
  bool up = WiFi.status() == WL_CONNECTED;
  if (up != wasUp) {
    wasUp = up;
    if (up) {
      LOGF("Wi-Fi up: '%s' %s", WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
      staConnectedSince = millis();
    } else {
      LOGF("Wi-Fi down");
      downSince = millis();
    }
  }
  // Network lost for a while (e.g. moved from home to the car hotspot):
  // rescan and take the first configured network that is on the air.
  if (!up && settings::anyWifi(cfg)) {
    uint32_t now = millis();
    uint32_t every = apActive ? 60000 : 30000;  // don't block the config AP too often
    if (now - downSince > 15000 && now - lastAttempt > every) {
      lastAttempt = now;
      WiFi.disconnect();
      if (!connectFirstAvailable(10000, false) && !apActive) startAccessPoint();
      up = WiFi.status() == WL_CONNECTED;
    }
  }
  // Only run FRN when credentials exist (avoids hammering the server).
  gateway.setNetworkUp(up);  // the gateway checks the credentials of the active system

  // Close the config AP once the station link has been stable for a minute.
  if (apActive && up && millis() - staConnectedSince > 60000) {
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
    apActive = false;
    LOGF("Config access point closed");
  }
  delay(2);
}
