// DTMF decoder (Goertzel), 8 kHz int16 input. Portable C++, no allocations.
//
// Blocks of 205 samples (25.6 ms). A digit is accepted when the same
// row+column pair is valid in 2 consecutive blocks; it must then disappear
// for 1 block (a normal inter-digit gap) before it can be reported again.
// Validity: strongest row and column tone above an absolute level, each
// clearly above the others of its group, limited twist, and the two tones
// carrying most of the block energy (rejects voice and single tones).
#pragma once
#include <math.h>
#include <stdint.h>

class DtmfDecoder {
 public:
  static constexpr int N = 205;

  DtmfDecoder() {
    static const float f[8] = {697, 770, 852, 941, 1209, 1336, 1477, 1633};
    for (int i = 0; i < 8; i++) coef_[i] = 2.0f * cosf(2.0f * (float)M_PI * f[i] / 8000.0f);
    reset();
  }
  void reset() {
    n_ = 0;
    energy_ = 0;
    for (int i = 0; i < 8; i++) s1_[i] = s2_[i] = 0;
    cand_ = 0;
    candCount_ = 0;
    current_ = 0;
    goneCount_ = 0;
  }

  // Feed samples. Returns a digit ('0'-'9', '*', '#', 'A'-'D') the moment it
  // is confirmed, 0 otherwise. If several digits complete inside one call
  // only the last one is returned: call with small chunks (<= N samples).
  char process(const int16_t* x, int count) {
    char out = 0;
    for (int k = 0; k < count; k++) {
      float v = x[k] * (1.0f / 32768.0f);
      energy_ += v * v;
      for (int i = 0; i < 8; i++) {
        float s0 = v + coef_[i] * s1_[i] - s2_[i];
        s2_[i] = s1_[i];
        s1_[i] = s0;
      }
      if (++n_ == N) {
        char d = endBlock();
        if (d) out = d;
      }
    }
    return out;
  }

  // True while a confirmed digit is still sounding (to mute it).
  bool active() const { return current_ != 0 || candCount_ > 0; }

 private:
  char classify() {
    float p[8];
    for (int i = 0; i < 8; i++) p[i] = s1_[i] * s1_[i] + s2_[i] * s2_[i] - coef_[i] * s1_[i] * s2_[i];
    int r = 0, c = 4;
    for (int i = 1; i < 4; i++)
      if (p[i] > p[r]) r = i;
    for (int i = 5; i < 8; i++)
      if (p[i] > p[c]) c = i;
    // Absolute level: amplitude ~0.02 FS per tone (-34 dBFS).
    const float minP = (0.02f * N / 2) * (0.02f * N / 2);
    if (p[r] < minP || p[c] < minP) return 0;
    // Twist: column vs row within +-8 dB.
    if (p[c] > p[r] * 6.3f || p[r] > p[c] * 6.3f) return 0;
    // Each tone well above the others of its group (6 dB).
    for (int i = 0; i < 4; i++)
      if (i != r && p[i] * 4.0f > p[r]) return 0;
    for (int i = 4; i < 8; i++)
      if (i != c && p[i] * 4.0f > p[c]) return 0;
    // The two tones must carry most of the energy (rejects voice).
    float total = energy_ * N / 2.0f;
    if (total <= 0 || (p[r] + p[c]) < 0.55f * total) return 0;
    static const char map[4][4] = {{'1', '2', '3', 'A'}, {'4', '5', '6', 'B'}, {'7', '8', '9', 'C'}, {'*', '0', '#', 'D'}};
    return map[r][c - 4];
  }

  char endBlock() {
    char d = classify();
    n_ = 0;
    energy_ = 0;
    for (int i = 0; i < 8; i++) s1_[i] = s2_[i] = 0;

    char report = 0;
    if (d) {
      goneCount_ = 0;
      if (d == cand_) {
        if (++candCount_ >= 2 && current_ != d) {
          current_ = d;
          report = d;
        }
      } else {
        cand_ = d;
        candCount_ = 1;
      }
    } else {
      candCount_ = 0;
      cand_ = 0;
      if (current_ && ++goneCount_ >= 1) {
        current_ = 0;
        goneCount_ = 0;
      }
    }
    return report;
  }

  float coef_[8], s1_[8], s2_[8];
  float energy_;
  int n_;
  char cand_, current_;
  int candCount_, goneCount_;
};
