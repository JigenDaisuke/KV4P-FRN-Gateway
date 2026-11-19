// Persistent settings (NVS). Everything is editable from the web page.
#pragma once
#include <Arduino.h>

constexpr int WIFI_SLOTS = 3;
constexpr int DTMF_SLOTS = 10;

struct Settings {
  // Wi-Fi: up to 3 networks, tried in this order (first one visible wins)
  String wifiSsid[WIFI_SLOTS];
  String wifiPass[WIFI_SLOTS];
  String hostname = "frn-gateway";
  String webPass = "frngateway";   // web page login: user "admin"

  // FRN
  String frnServer = "server.voip-italia.net";
  uint16_t frnPort = 10024;
  String frnBackupServer;
  uint16_t frnBackupPort = 0;
  String frnVersion = "2014003";
  String frnEmail;
  String frnPassword;
  String frnCallsign;            // e.g. "IZ0ABC"
  String frnName;                // e.g. "Mario"  -> <ON>IZ0ABC, Mario</ON>
  String frnNet = "Nazionale";
  String frnDescription = "FRN ITALIA - Gateway KV4P ESP32";
  String frnCountry = "Italy";
  String frnCity;                // "Roma - Centro"
  String frnBandChannel;         // empty = generated from radio settings
  uint8_t frnClientType = 1;     // 0 = Crosslink, 1 = Gateway, 2 = PC Only

  // Radio (SA818 / DRA818)
  bool uhf = false;              // module variant: false = VHF (134-174), true = UHF (400-480)
  float freqRx = 145.5000f;      // MHz
  float freqTx = 145.5000f;      // MHz (simplex gateway: same as RX)
  uint8_t bandwidth = 1;         // 0 = 12.5 kHz, 1 = 25 kHz
  uint8_t ctcssTx = 0;           // 0 = none, 1..38 = standard tone index
  uint8_t ctcssRx = 0;           // 0 = none (open squelch on carrier)
  uint8_t squelch = 4;           // SA818 squelch 1..8 (0 = always open, not for gateways)
  uint8_t volume = 6;            // SA818 audio out 1..8
  bool filterEmphasis = true;    // SA818 pre/de-emphasis
  bool filterHighpass = true;    // SA818 300 Hz high-pass (removes CTCSS from audio)
  bool filterLowpass = true;     // SA818 3 kHz low-pass
  bool highPower = true;         // H/L: true = high (~1 W), false = low (~0.5 W)
  // How the module H/L pin is wired:
  //   0 = not connected (KV4P v1.x stock: module always at high power)
  //   1 = GPIO23 wired straight to H/L (v1.x mod): LOW = low power, open = high
  //   2 = GPIO23 through a transistor (KV4P v2.0): LOW = high power, HIGH = low
  uint8_t powerPinMode = 0;

  // Gateway behaviour
  uint8_t sqMode = 0;            // 0 = SA818 SQ pin, 1 = audio level (VOX), 2 = both
  uint16_t voxThreshold = 600;   // RMS (int16) for VOX mode
  uint16_t sqOpenMs = 80;        // carrier must be present this long before we go to the net
  uint16_t sqHangMs = 400;       // keep the net TX this long after carrier drops
  uint16_t txDelayMs = 250;      // PTT -> audio delay (lets the remote receivers open)
  uint16_t txTailMs = 150;       // silence after audio before PTT release
  uint16_t rxLockoutMs = 600;    // ignore local squelch after our own TX
  uint16_t jitterMs = 400;       // net audio buffered before playing
  uint16_t totSec = 180;         // time-out timer, both directions
  float rxGain = 1.0f;           // radio -> net digital gain
  float txGain = 1.0f;           // net -> radio digital gain

  // Courtesy tones ("coda" like a repeater). Preset 0 = off, see tones.h
  uint8_t toneQsoEnd = 1;        // after every over received from FRN
  uint16_t toneQsoEndHz = 1000;
  uint8_t toneLocal = 3;         // after MY over, when it really went out on FRN
  uint16_t toneLocalHz = 1200;
  uint8_t toneBusy = 0;          // after my over when the gateway could NOT take it
  uint16_t toneBusyHz = 500;
  uint16_t toneLevel = 6000;     // amplitude (int16)

  // DTMF room change: send "#<code>" from the radio, the end of the over
  // confirms. "#0" always returns to the main room (frnNet).
  bool dtmfEnabled = true;
  String dtmfCode[DTMF_SLOTS] = {"1", "2", "3", "999"};
  String dtmfRoom[DTMF_SLOTS] = {"Nazionale", "Prove", "Ham Radio Network", "Test"};
  uint8_t dtmfReturnMin = 0;     // back to the main room after N min without traffic (0 = never)

  // CW identification on the radio side
  bool cwIdEnabled = true;
  uint8_t cwIdMinutes = 10;      // send callsign in CW after an over, at most every N min
  uint8_t cwWpm = 20;
  uint16_t cwToneHz = 800;
  uint16_t cwLevel = 6000;       // amplitude (int16)
};

namespace settings {
void load(Settings& s);
void save(const Settings& s);
void factoryReset();
// "145.50000FM CTC88.5" style string for the FRN <BC> field
// ("Crosslink" / "PC Only" for those client types).
String bandChannel(const Settings& s);
bool anyWifi(const Settings& s);
// Standard CTCSS table, index 1..38 (index 0 = none).
float ctcssHz(uint8_t index);
}  // namespace settings
