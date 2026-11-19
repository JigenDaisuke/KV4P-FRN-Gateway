#include "settings.h"

#include <Preferences.h>

namespace {
const char* NS = "frngw";
const float CTCSS[39] = {0,     67.0,  71.9,  74.4,  77.0,  79.7,  82.5,  85.4,  88.5,  91.5,
                         94.8,  97.4,  100.0, 103.5, 107.2, 110.9, 114.8, 118.8, 123.0, 127.3,
                         131.8, 136.5, 141.3, 146.2, 151.4, 156.7, 162.2, 167.9, 173.8, 179.9,
                         186.2, 192.8, 203.5, 210.7, 218.1, 225.7, 233.6, 241.8, 250.3};
}  // namespace

namespace settings {

float ctcssHz(uint8_t index) { return index <= 38 ? CTCSS[index] : 0.0f; }

void load(Settings& s) {
  Preferences p;
  p.begin(NS, true);
  for (int i = 0; i < WIFI_SLOTS; i++) {  // slot 0 keeps the v0.2 key names
    String k = i ? String(i) : String("");
    s.wifiSsid[i] = p.getString(("wifiSsid" + k).c_str(), s.wifiSsid[i]);
    s.wifiPass[i] = p.getString(("wifiPass" + k).c_str(), s.wifiPass[i]);
  }
  s.hostname = p.getString("hostname", s.hostname);
  s.webPass = p.getString("webPass", s.webPass);

  s.frnServer = p.getString("frnServer", s.frnServer);
  s.frnPort = p.getUShort("frnPort", s.frnPort);
  s.frnBackupServer = p.getString("frnBkServer", s.frnBackupServer);
  s.frnBackupPort = p.getUShort("frnBkPort", s.frnBackupPort);
  s.frnVersion = p.getString("frnVersion", s.frnVersion);
  s.frnEmail = p.getString("frnEmail", s.frnEmail);
  s.frnPassword = p.getString("frnPassword", s.frnPassword);
  s.frnCallsign = p.getString("frnCallsign", s.frnCallsign);
  s.frnName = p.getString("frnName", s.frnName);
  s.frnNet = p.getString("frnNet", s.frnNet);
  s.frnDescription = p.getString("frnDescr", s.frnDescription);
  s.frnCountry = p.getString("frnCountry", s.frnCountry);
  s.frnCity = p.getString("frnCity", s.frnCity);
  s.frnBandChannel = p.getString("frnBC", s.frnBandChannel);
  s.frnClientType = p.getUChar("frnCl", s.frnClientType);

  s.uhf = p.getBool("uhf", s.uhf);
  s.freqRx = p.getFloat("freqRx", s.freqRx);
  s.freqTx = p.getFloat("freqTx", s.freqTx);
  s.bandwidth = p.getUChar("bw", s.bandwidth);
  s.ctcssTx = p.getUChar("ctcssTx", s.ctcssTx);
  s.ctcssRx = p.getUChar("ctcssRx", s.ctcssRx);
  s.squelch = p.getUChar("squelch", s.squelch);
  s.volume = p.getUChar("volume", s.volume);
  s.filterEmphasis = p.getBool("fEmph", s.filterEmphasis);
  s.filterHighpass = p.getBool("fHp", s.filterHighpass);
  s.filterLowpass = p.getBool("fLp", s.filterLowpass);
  s.highPower = p.getBool("hiPow", s.highPower);
  s.powerPinMode = p.getUChar("powPin", s.powerPinMode);

  s.sqMode = p.getUChar("sqMode", s.sqMode);
  s.voxThreshold = p.getUShort("voxThr", s.voxThreshold);
  s.sqOpenMs = p.getUShort("sqOpenMs", s.sqOpenMs);
  s.sqHangMs = p.getUShort("sqHangMs", s.sqHangMs);
  s.txDelayMs = p.getUShort("txDelayMs", s.txDelayMs);
  s.txTailMs = p.getUShort("txTailMs", s.txTailMs);
  s.rxLockoutMs = p.getUShort("rxLockMs", s.rxLockoutMs);
  s.jitterMs = p.getUShort("jitterMs", s.jitterMs);
  s.totSec = p.getUShort("totSec", s.totSec);
  s.rxGain = p.getFloat("rxGain", s.rxGain);
  s.txGain = p.getFloat("txGain", s.txGain);

  s.toneQsoEnd = p.getUChar("tQso", s.toneQsoEnd);
  s.toneQsoEndHz = p.getUShort("tQsoHz", s.toneQsoEndHz);
  s.toneLocal = p.getUChar("tLoc", s.toneLocal);
  s.toneLocalHz = p.getUShort("tLocHz", s.toneLocalHz);
  s.toneBusy = p.getUChar("tBusy", s.toneBusy);
  s.toneBusyHz = p.getUShort("tBusyHz", s.toneBusyHz);
  s.toneLevel = p.getUShort("tLevel", s.toneLevel);

  s.dtmfEnabled = p.getBool("dtmfOn", s.dtmfEnabled);
  s.dtmfReturnMin = p.getUChar("dtmfRet", s.dtmfReturnMin);
  if (p.isKey("dCode0")) {  // table saved at least once: take it as is
    for (int i = 0; i < DTMF_SLOTS; i++) {
      s.dtmfCode[i] = p.getString(("dCode" + String(i)).c_str(), "");
      s.dtmfRoom[i] = p.getString(("dRoom" + String(i)).c_str(), "");
    }
  }

  s.cwIdEnabled = p.getBool("cwId", s.cwIdEnabled);
  s.cwIdMinutes = p.getUChar("cwIdMin", s.cwIdMinutes);
  s.cwWpm = p.getUChar("cwWpm", s.cwWpm);
  s.cwToneHz = p.getUShort("cwTone", s.cwToneHz);
  s.cwLevel = p.getUShort("cwLevel", s.cwLevel);
  p.end();
}

void save(const Settings& s) {
  Preferences p;
  p.begin(NS, false);
  for (int i = 0; i < WIFI_SLOTS; i++) {
    String k = i ? String(i) : String("");
    p.putString(("wifiSsid" + k).c_str(), s.wifiSsid[i]);
    p.putString(("wifiPass" + k).c_str(), s.wifiPass[i]);
  }
  p.putString("hostname", s.hostname);
  p.putString("webPass", s.webPass);

  p.putString("frnServer", s.frnServer);
  p.putUShort("frnPort", s.frnPort);
  p.putString("frnBkServer", s.frnBackupServer);
  p.putUShort("frnBkPort", s.frnBackupPort);
  p.putString("frnVersion", s.frnVersion);
  p.putString("frnEmail", s.frnEmail);
  p.putString("frnPassword", s.frnPassword);
  p.putString("frnCallsign", s.frnCallsign);
  p.putString("frnName", s.frnName);
  p.putString("frnNet", s.frnNet);
  p.putString("frnDescr", s.frnDescription);
  p.putString("frnCountry", s.frnCountry);
  p.putString("frnCity", s.frnCity);
  p.putString("frnBC", s.frnBandChannel);
  p.putUChar("frnCl", s.frnClientType);

  p.putBool("uhf", s.uhf);
  p.putFloat("freqRx", s.freqRx);
  p.putFloat("freqTx", s.freqTx);
  p.putUChar("bw", s.bandwidth);
  p.putUChar("ctcssTx", s.ctcssTx);
  p.putUChar("ctcssRx", s.ctcssRx);
  p.putUChar("squelch", s.squelch);
  p.putUChar("volume", s.volume);
  p.putBool("fEmph", s.filterEmphasis);
  p.putBool("fHp", s.filterHighpass);
  p.putBool("fLp", s.filterLowpass);
  p.putBool("hiPow", s.highPower);
  p.putUChar("powPin", s.powerPinMode);

  p.putUChar("sqMode", s.sqMode);
  p.putUShort("voxThr", s.voxThreshold);
  p.putUShort("sqOpenMs", s.sqOpenMs);
  p.putUShort("sqHangMs", s.sqHangMs);
  p.putUShort("txDelayMs", s.txDelayMs);
  p.putUShort("txTailMs", s.txTailMs);
  p.putUShort("rxLockMs", s.rxLockoutMs);
  p.putUShort("jitterMs", s.jitterMs);
  p.putUShort("totSec", s.totSec);
  p.putFloat("rxGain", s.rxGain);
  p.putFloat("txGain", s.txGain);

  p.putUChar("tQso", s.toneQsoEnd);
  p.putUShort("tQsoHz", s.toneQsoEndHz);
  p.putUChar("tLoc", s.toneLocal);
  p.putUShort("tLocHz", s.toneLocalHz);
  p.putUChar("tBusy", s.toneBusy);
  p.putUShort("tBusyHz", s.toneBusyHz);
  p.putUShort("tLevel", s.toneLevel);

  p.putBool("dtmfOn", s.dtmfEnabled);
  p.putUChar("dtmfRet", s.dtmfReturnMin);
  for (int i = 0; i < DTMF_SLOTS; i++) {
    p.putString(("dCode" + String(i)).c_str(), s.dtmfCode[i]);
    p.putString(("dRoom" + String(i)).c_str(), s.dtmfRoom[i]);
  }

  p.putBool("cwId", s.cwIdEnabled);
  p.putUChar("cwIdMin", s.cwIdMinutes);
  p.putUChar("cwWpm", s.cwWpm);
  p.putUShort("cwTone", s.cwToneHz);
  p.putUShort("cwLevel", s.cwLevel);
  p.end();
}

void factoryReset() {
  Preferences p;
  p.begin(NS, false);
  p.clear();
  p.end();
}

bool anyWifi(const Settings& s) {
  for (int i = 0; i < WIFI_SLOTS; i++)
    if (s.wifiSsid[i].length()) return true;
  return false;
}

String bandChannel(const Settings& s) {
  if (s.frnClientType == 0) return String("Crosslink");
  if (s.frnClientType == 2) return String("PC Only");
  if (s.frnBandChannel.length() > 0) return s.frnBandChannel;
  char buf[48];
  float tone = ctcssHz(s.ctcssTx);
  if (tone > 0)
    snprintf(buf, sizeof(buf), "%.5fFM CTC%.1f", (double)s.freqRx, (double)tone);
  else
    snprintf(buf, sizeof(buf), "%.5fFM", (double)s.freqRx);
  return String(buf);
}

}  // namespace settings
