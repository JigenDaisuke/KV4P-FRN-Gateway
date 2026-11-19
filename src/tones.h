// Courtesy tones ("coda" like a repeater), 8 kHz int16. Portable.
// Presets: 0 off, 1 short beep, 2 double beep, 3 rising, 4 falling,
//          5 three tones up, 6 trill, 7 "K" in CW, 8 "R" in CW.
#pragma once
#include <math.h>
#include <stdint.h>

#include "cw.h"

namespace tones {
constexpr int PRESETS = 9;
inline const char* presetName(int p) {
  static const char* n[PRESETS] = {"Nessuno",         "Beep corto",   "Beep doppio",
                                   "Bip-bop (sale)",  "Bop-bip (scende)", "Tre toni",
                                   "Trillo",          "K in CW",      "R in CW (roger)"};
  return (p >= 0 && p < PRESETS) ? n[p] : "?";
}
}  // namespace tones

class ToneSequence {
 public:
  // baseHz: pitch of the preset; level: amplitude (int16)
  void start(int preset, int baseHz, int level) {
    n_ = 0;
    seg_ = 0;
    pos_ = 0;
    phase_ = 0;
    useCw_ = false;
    amp_ = (float)level;
    const float f = (float)baseHz;
    switch (preset) {
      case 1: add(f, 150); break;
      case 2: add(f, 90); add(0, 70); add(f, 90); break;
      case 3: add(f * 0.8f, 110); add(f * 1.25f, 130); break;
      case 4: add(f * 1.25f, 110); add(f * 0.8f, 130); break;
      case 5: add(f * 0.8f, 90); add(f, 90); add(f * 1.25f, 110); break;
      case 6:
        for (int i = 0; i < 4; i++) {
          add(f, 45);
          add(f * 1.26f, 45);
        }
        break;
      case 7:
      case 8:
        useCw_ = true;
        cw_.start(preset == 7 ? "K" : "R", 22, baseHz, level, 0);
        break;
      default: break;  // 0 = off
    }
    if (!useCw_ && n_ > 0) add(0, 60);  // short silence after the tone
    done_ = !useCw_ && n_ == 0;
  }
  bool done() const { return useCw_ ? cw_.done() : done_; }

  size_t fill(int16_t* out, size_t n) {
    if (useCw_) return cw_.fill(out, n);
    size_t k = 0;
    while (k < n && !done_) {
      if (seg_ >= n_) {
        done_ = true;
        break;
      }
      const Seg& s = segs_[seg_];
      float v = 0;
      if (s.hz > 0) {
        const uint32_t ramp = 40;  // 5 ms raised-cosine edges, no clicks
        float env = 1.0f;
        if (pos_ < ramp) env = 0.5f - 0.5f * cosf((float)M_PI * pos_ / ramp);
        else if (s.len - pos_ < ramp) env = 0.5f - 0.5f * cosf((float)M_PI * (s.len - pos_) / ramp);
        v = amp_ * env * sinf(phase_);
        phase_ += 2.0f * (float)M_PI * s.hz / 8000.0f;
        if (phase_ > 2.0f * (float)M_PI) phase_ -= 2.0f * (float)M_PI;
      }
      out[k++] = (int16_t)v;
      if (++pos_ >= s.len) {
        pos_ = 0;
        seg_++;
      }
    }
    return k;
  }

 private:
  struct Seg {
    float hz;
    uint32_t len;
  };
  void add(float hz, int ms) {
    if (n_ < 12) segs_[n_++] = Seg{hz, (uint32_t)(ms * 8)};
  }
  Seg segs_[12];
  int n_ = 0, seg_ = 0;
  uint32_t pos_ = 0;
  float phase_ = 0, amp_ = 0;
  bool done_ = true, useCw_ = false;
  CwGenerator cw_;
};
