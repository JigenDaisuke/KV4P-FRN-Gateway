#include "audio_io.h"

#include <driver/adc.h>
#include <driver/dac.h>
#include <driver/i2s.h>

#include "board_kv4p.h"

namespace {
constexpr i2s_port_t PORT = I2S_NUM_0;
constexpr int DMA_LEN = 256;   // samples per DMA buffer
constexpr int DMA_COUNT = 6;
constexpr adc1_channel_t ADC_CH = ADC1_CHANNEL_6;  // GPIO34
}  // namespace

bool AudioIO::begin() {
  dc_.begin(120.0f, HW_RATE);
  dec_.begin(HW_RATE, 3000.0f);
  interp_.begin(HW_RATE, 3000.0f);
  frac_.setStep(1.0);
  adc1_config_width(ADC_WIDTH_BIT_12);
  adc1_config_channel_atten(ADC_CH, ADC_ATTEN_DB_12);
  applyBias();
  setMode(Mode::Rx);
  return true;
}

void AudioIO::applyBias() {
  // DC bias for the SA818 audio-out coupling network (same as kv4p firmware).
  dac_output_enable(DAC_CHANNEL_2);
  dac_output_voltage(DAC_CHANNEL_2, (uint8_t)(255.0f / 3.3f * board::ADC_BIAS_VOLTS));
}

void AudioIO::stopI2s() {
  if (mode_ == Mode::Rx) {
    i2s_adc_disable(PORT);
    i2s_driver_uninstall(PORT);
  } else if (mode_ == Mode::Tx) {
    i2s_zero_dma_buffer(PORT);
    i2s_driver_uninstall(PORT);
    // Leave the mic input high-impedance between overs (avoids a pop).
    pinMode(board::PIN_AUDIO_OUT, INPUT);
  }
  mode_ = Mode::Off;
}

void AudioIO::setMode(Mode m) {
  if (m == mode_) return;
  stopI2s();
  if (m == Mode::Rx) startRx();
  if (m == Mode::Tx) startTx();
}

void AudioIO::startRx() {
  applyBias();  // uninstalling the DAC-mode driver switches both DACs off
  i2s_config_t cfg = {};
  cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX | I2S_MODE_ADC_BUILT_IN);
  cfg.sample_rate = HW_RATE;
  cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
  cfg.channel_format = I2S_CHANNEL_FMT_ONLY_LEFT;
  cfg.communication_format = I2S_COMM_FORMAT_STAND_MSB;
  cfg.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
  cfg.dma_buf_count = DMA_COUNT;
  cfg.dma_buf_len = DMA_LEN;
  cfg.use_apll = false;
  cfg.tx_desc_auto_clear = false;
  i2s_driver_install(PORT, &cfg, 0, nullptr);
  i2s_set_adc_mode(ADC_UNIT_1, ADC_CH);
  adc1_config_channel_atten(ADC_CH, ADC_ATTEN_DB_12);
  i2s_adc_enable(PORT);
  dc_.reset();
  dec_.reset();
  frac_.reset();
  rxStartMs_ = millis();
  measStartMs_ = 0;
  measSamples_ = 0;
  mode_ = Mode::Rx;
}

void AudioIO::startTx() {
  i2s_config_t cfg = {};
  cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX | I2S_MODE_DAC_BUILT_IN);
  cfg.sample_rate = HW_RATE;
  cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
  cfg.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
  cfg.communication_format = I2S_COMM_FORMAT_STAND_MSB;
  cfg.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
  cfg.dma_buf_count = DMA_COUNT;
  cfg.dma_buf_len = DMA_LEN;
  cfg.use_apll = false;
  cfg.tx_desc_auto_clear = true;  // play silence on underrun instead of repeating
  i2s_driver_install(PORT, &cfg, 0, nullptr);
  i2s_set_pin(PORT, nullptr);                     // built-in DAC
  i2s_set_dac_mode(I2S_DAC_CHANNEL_RIGHT_EN);     // DAC1 = GPIO25 only
  i2s_zero_dma_buffer(PORT);
  interp_.reset();
  mode_ = Mode::Tx;
}

size_t AudioIO::readRx8k(int16_t* out, size_t maxOut) {
  if (mode_ != Mode::Rx) return 0;
  uint16_t raw[DMA_LEN];
  size_t br = 0;
  i2s_read(PORT, raw, sizeof(raw), &br, pdMS_TO_TICKS(50));
  size_t n = br / 2;
  if (n == 0) return 0;

  // Measure the real ADC sample rate and trim the 8 kHz output with it.
  uint32_t now = millis();
  if (measStartMs_ == 0) {
    if (now - rxStartMs_ > 500) {  // skip the start-up burst
      measStartMs_ = now;
      measSamples_ = 0;
    }
  } else {
    measSamples_ += n;
    uint32_t dt = now - measStartMs_;
    if (dt >= 5000) {
      float r = measSamples_ * 1000.0f / dt;
      if (r > HW_RATE * 0.8f && r < HW_RATE * 1.2f) {
        adcRate_ = 0.7f * adcRate_ + 0.3f * r;
        frac_.setStep((adcRate_ / FACTOR) / 8000.0);
      }
      measStartMs_ = now;
      measSamples_ = 0;
    }
  }

  size_t produced = 0;
  // The built-in ADC delivers samples swapped in pairs; 12-bit value in the low bits.
  for (size_t i = 0; i + 1 < n; i += 2) {
    uint16_t pair[2] = {raw[i + 1], raw[i]};
    for (int k = 0; k < 2; k++) {
      float x = ((int)(pair[k] & 0x0FFF) - 2048) * 16.0f;
      x = dc_.process(x);
      float y;
      if (dec_.push(x, &y)) {
        float o[2];
        int c = frac_.push(y * rxGain, o);
        for (int j = 0; j < c && produced < maxOut; j++) out[produced++] = dsp::clip16(o[j]);
      }
    }
  }
  return produced;
}

void AudioIO::writeTx8k(const int16_t* in, size_t n) {
  if (mode_ != Mode::Tx) return;
  // Each 8 kHz sample -> 6 samples @ 48 kHz -> stereo frame (L = R).
  constexpr size_t CHUNK = 40;  // 40 in -> 240 frames -> 960 bytes
  uint16_t buf[CHUNK * FACTOR * 2];
  while (n > 0) {
    size_t c = n < CHUNK ? n : CHUNK;
    size_t k = 0;
    for (size_t i = 0; i < c; i++) {
      float y[FACTOR];
      interp_.push(in[i] * txGain, y);
      for (int p = 0; p < FACTOR; p++) {
        // Built-in DAC takes the high byte, unsigned.
        uint16_t u = (uint16_t)(dsp::clip16(y[p]) + 32768);
        buf[k++] = u;
        buf[k++] = u;
      }
    }
    size_t bw = 0;
    i2s_write(PORT, buf, k * sizeof(uint16_t), &bw, portMAX_DELAY);
    in += c;
    n -= c;
  }
}
