// Integer-ratio polyphase resamplers (48 kHz <-> 8 kHz) with a windowed-sinc
// low-pass filter, plus a simple DC-blocking high-pass. Portable C++ (no
// Arduino dependencies) so it can be unit-tested on a PC.
#pragma once
#include <math.h>
#include <stdint.h>
#include <string.h>

namespace dsp {

// Windowed-sinc low-pass FIR, normalised to unity DC gain.
template <int TAPS>
inline void designLowpass(float* h, float cutoffHz, float fs) {
  const float fc = cutoffHz / fs;
  const float M = TAPS - 1;
  float sum = 0;
  for (int n = 0; n < TAPS; n++) {
    float x = n - M / 2.0f;
    float sinc = (fabsf(x) < 1e-6f) ? 2.0f * fc : sinf(2.0f * (float)M_PI * fc * x) / ((float)M_PI * x);
    // Blackman window
    float w = 0.42f - 0.5f * cosf(2.0f * (float)M_PI * n / M) + 0.08f * cosf(4.0f * (float)M_PI * n / M);
    h[n] = sinc * w;
    sum += h[n];
  }
  for (int n = 0; n < TAPS; n++) h[n] /= sum;
}

inline int16_t clip16(float v) {
  if (v > 32767.0f) return 32767;
  if (v < -32768.0f) return -32768;
  return (int16_t)lrintf(v);
}

// Decimate by FACTOR: feed input samples, get one output every FACTOR inputs.
template <int FACTOR, int TAPS>
class Decimator {
 public:
  void begin(float fsIn, float cutoffHz) {
    designLowpass<TAPS>(h_, cutoffHz, fsIn);
    reset();
  }
  void reset() {
    memset(buf_, 0, sizeof(buf_));
    pos_ = 0;
    phase_ = 0;
  }
  // Returns true and writes *out when an output sample is ready.
  bool push(float x, float* out) {
    buf_[pos_] = x;
    buf_[pos_ + TAPS] = x;  // mirrored buffer: contiguous window, no modulo in loop
    pos_ = (pos_ + 1) % TAPS;
    if (++phase_ < FACTOR) return false;
    phase_ = 0;
    const float* w = &buf_[pos_];  // oldest .. newest
    float acc = 0;
    for (int k = 0; k < TAPS; k++) acc += h_[k] * w[k];
    *out = acc;
    return true;
  }

 private:
  float h_[TAPS];
  float buf_[2 * TAPS];
  int pos_ = 0;
  int phase_ = 0;
};

// Interpolate by FACTOR: each input produces FACTOR outputs.
template <int FACTOR, int TAPS>
class Interpolator {
  static_assert(TAPS % FACTOR == 0, "TAPS must be a multiple of FACTOR");
  static constexpr int PHASE_TAPS = TAPS / FACTOR;

 public:
  void begin(float fsOut, float cutoffHz) {
    float h[TAPS];
    designLowpass<TAPS>(h, cutoffHz, fsOut);
    // Split into polyphase branches; gain FACTOR to compensate zero-stuffing.
    for (int p = 0; p < FACTOR; p++)
      for (int k = 0; k < PHASE_TAPS; k++) poly_[p][k] = h[k * FACTOR + p] * FACTOR;
    reset();
  }
  void reset() {
    memset(buf_, 0, sizeof(buf_));
    pos_ = 0;
  }
  // Writes FACTOR samples to out.
  void push(float x, float* out) {
    buf_[pos_] = x;
    buf_[pos_ + PHASE_TAPS] = x;
    pos_ = (pos_ + 1) % PHASE_TAPS;
    const float* w = &buf_[pos_];  // oldest .. newest
    for (int p = 0; p < FACTOR; p++) {
      float acc = 0;
      // newest sample pairs with poly[p][0]
      for (int k = 0; k < PHASE_TAPS; k++) acc += poly_[p][k] * w[PHASE_TAPS - 1 - k];
      out[p] = acc;
    }
  }

 private:
  float poly_[FACTOR][PHASE_TAPS];
  float buf_[2 * PHASE_TAPS];
  int pos_ = 0;
};

// Fractional resampler (linear interpolation) used to trim small clock
// errors (< +-10 %) after the integer decimator. step = inRate / outRate.
class FracResampler {
 public:
  void setStep(double step) {
    if (step < 0.9) step = 0.9;
    if (step > 1.1) step = 1.1;
    step_ = step;
  }
  double step() const { return step_; }
  void reset() {
    t_ = 0;
    prev_ = 0;
  }
  // Feed one input sample; writes 0..2 outputs, returns count.
  int push(float x, float* out) {
    int n = 0;
    while (t_ < 1.0 && n < 2) {
      out[n++] = prev_ + (float)t_ * (x - prev_);
      t_ += step_;
    }
    t_ -= 1.0;
    if (t_ < 0) t_ = 0;
    prev_ = x;
    return n;
  }

 private:
  double step_ = 1.0, t_ = 0;
  float prev_ = 0;
};

// First-order DC blocker / gentle high-pass: y = x - x[-1] + r*y[-1]
class DcBlocker {
 public:
  void begin(float cornerHz, float fs) {
    r_ = 1.0f - (2.0f * (float)M_PI * cornerHz / fs);
    if (r_ < 0.5f) r_ = 0.5f;
    reset();
  }
  void reset() { x1_ = y1_ = 0; }
  float process(float x) {
    float y = x - x1_ + r_ * y1_;
    x1_ = x;
    y1_ = y;
    return y;
  }

 private:
  float r_ = 0.995f, x1_ = 0, y1_ = 0;
};

}  // namespace dsp
