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
#include "web_ui.h"

#define FW_VERSION "0.2.0"

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
  } else {
    LOGF("SA818 NOT responding - check module / power");
  }

  WiFi.persistent(false);
  WiFi.setHostname(cfg.hostname.c_str());
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);  // modem sleep adds latency and jitter to the audio stream
  WiFi.setAutoReconnect(true);
  if (cfg.wifiSsid.length()) {
    LOGF("Wi-Fi: connecting to '%s'", cfg.wifiSsid.c_str());
    WiFi.begin(cfg.wifiSsid.c_str(), cfg.wifiPass.c_str());
    uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < 20000) {
      digitalWrite(board::PIN_LED, (millis() / 250) & 1);
      delay(50);
    }
  }
  if (WiFi.status() == WL_CONNECTED) {
    LOGF("Wi-Fi OK: %s  RSSI %d dBm  -> http://%s/ or http://%s.local/", WiFi.localIP().toString().c_str(),
         WiFi.RSSI(), WiFi.localIP().toString().c_str(), cfg.hostname.c_str());
    staConnectedSince = millis();
  } else {
    startAccessPoint();
  }
  if (MDNS.begin(cfg.hostname.c_str())) MDNS.addService("http", "tcp", 80);

  webui::begin(cfg, gateway);
  gateway.begin(cfg, radio);
  if (!frnProfileComplete())
    LOGF("FRN profile incomplete: fill in e-mail, password, callsign, name, city and country on the web page");
}

void loop() {
  webui::loop();

  static bool wasUp = false;
  bool up = WiFi.status() == WL_CONNECTED;
  if (up != wasUp) {
    wasUp = up;
    if (up) {
      LOGF("Wi-Fi up: %s", WiFi.localIP().toString().c_str());
      staConnectedSince = millis();
    } else {
      LOGF("Wi-Fi down");
    }
  }
  // Only run FRN when credentials exist (avoids hammering the server).
  gateway.setNetworkUp(up && frnProfileComplete());

  // Close the config AP once the station link has been stable for a minute.
  if (apActive && up && millis() - staConnectedSince > 60000) {
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
    apActive = false;
    LOGF("Config access point closed");
  }
  delay(2);
}
