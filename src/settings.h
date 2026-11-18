// Persistent settings (NVS). Everything is editable from the web page.
#pragma once
#include <Arduino.h>

struct Settings {
  // Wi-Fi
  String wifiSsid;
  String wifiPass;
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

  // Radio (SA818)
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
// "145.50000FM CTC88.5" style string for the FRN <BC> field.
String bandChannel(const Settings& s);
// Standard CTCSS table, index 1..38 (index 0 = none).
float ctcssHz(uint8_t index);
}  // namespace settings
