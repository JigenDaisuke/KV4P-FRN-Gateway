// Audio I/O on the KV4P: SA818 audio out -> ESP32 ADC1 (GPIO34) via I2S0
// built-in ADC mode; ESP32 DAC1 (GPIO25) -> SA818 mic via I2S0 built-in DAC
// mode. I2S0 is the only port with the built-in ADC/DAC, so RX and TX are
// mutually exclusive (fine for a half-duplex gateway). Hardware runs at
// 48 kHz; this class converts to/from the 8 kHz used by FRN.
#pragma once
#include <Arduino.h>

#include "resampler.h"

class AudioIO {
 public:
  enum class Mode : uint8_t { Off, Rx, Tx };

  bool begin();
  // Must be called from the audio task only.
  void setMode(Mode m);
  Mode mode() const { return mode_; }

  // RX: read one DMA chunk, return number of 8 kHz samples written (<= maxOut).
  size_t readRx8k(int16_t* out, size_t maxOut);
  // TX: play n samples @ 8 kHz (blocks for pacing).
  void writeTx8k(const int16_t* in, size_t n);

  float adcRateHz() const { return adcRate_; }
  float rxGain = 1.0f;
  float txGain = 1.0f;

  static constexpr int HW_RATE = 48000;
  static constexpr int FACTOR = 6;  // 48 kHz / 8 kHz

 private:
  void startRx();
  void startTx();
  void stopI2s();
  void applyBias();

  Mode mode_ = Mode::Off;
  dsp::DcBlocker dc_;
  dsp::Decimator<FACTOR, 144> dec_;
  dsp::FracResampler frac_;
  dsp::Interpolator<FACTOR, 144> interp_;

  // ADC clock measurement (the ESP32 I2S-ADC clock is not exactly 48 kHz).
  uint32_t measStartMs_ = 0;
  uint32_t measSamples_ = 0;
  uint32_t rxStartMs_ = 0;
  float adcRate_ = HW_RATE;
};
