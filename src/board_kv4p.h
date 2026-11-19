// KV4P HT v1.x pin map (v1.5 .. v1.8). Taken from the official kv4p-ht
// firmware (microcontroller-src/.../globals.h, board.h), "V1" defaults.
#pragma once

namespace board {
constexpr int PIN_RF_RXD    = 16;  // ESP32 RX  <- SA818 TXD
constexpr int PIN_RF_TXD    = 17;  // ESP32 TX  -> SA818 RXD
constexpr int PIN_PTT       = 18;  // SA818 PTT: LOW = transmit, HIGH = receive
constexpr int PIN_PD        = 19;  // SA818 PD : HIGH = powered on
constexpr int PIN_SQ        = 32;  // SA818 SQ : HIGH = squelch closed (no signal)
constexpr int PIN_AUDIO_IN  = 34;  // SA818 AF out -> ADC1_CHANNEL_6
constexpr int PIN_AUDIO_OUT = 25;  // DAC1 -> SA818 MIC in
constexpr int PIN_ADC_BIAS  = 26;  // DAC2 = DC bias for the audio input network
constexpr int PIN_HL        = 23;  // SA818 H/L (power): wired only on KV4P v2.0 or with a mod
constexpr int PIN_LED       = 2;   // on-board blue LED of the ESP32 module
constexpr int PIN_BOOT_BTN  = 0;   // BOOT button (hold 5 s at power-up = factory reset)

constexpr float ADC_BIAS_VOLTS = 1.75f;  // same as kv4p firmware default
constexpr int SA818_BAUD = 9600;
}  // namespace board
