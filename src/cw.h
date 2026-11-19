// Morse (CW) identification tone generator, 8 kHz int16. Portable.
#pragma once
#include <ctype.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

class CwGenerator {
 public:
  void start(const char* text, int wpm, int toneHz, int amplitude, int leadMs = 250, int tailMs = 200) {
    strncpy(text_, text, sizeof(text_) - 1);
    text_[sizeof(text_) - 1] = 0;
    dot_ = (uint32_t)(8000 * 1.2f / (wpm < 5 ? 5 : wpm));  // samples per dot
    w_ = 2.0f * (float)M_PI * toneHz / 8000.0f;
    amp_ = (float)amplitude;
    ch_ = 0;
    sym_ = nullptr;
    remain_ = (uint32_t)(leadMs * 8);  // leading silence
    tail_ = (uint32_t)(tailMs * 8);
    on_ = false;
    phase_ = 0;
    pos_ = 0;
    len_ = remain_;
    done_ = false;
    gap_ = false;
    tailDone_ = false;
  }
  bool done() const { return done_; }

  // Fill up to n samples. Returns samples written (0 when finished).
  size_t fill(int16_t* out, size_t n) {
    size_t k = 0;
    while (k < n && !done_) {
      if (remain_ == 0 && !next()) {
        done_ = true;
        break;
      }
      float s = 0;
      if (on_) {
        // 5 ms raised-cosine ramps against key clicks
        const uint32_t ramp = 40;
        float env = 1.0f;
        if (pos_ < ramp) env = 0.5f - 0.5f * cosf((float)M_PI * pos_ / ramp);
        else if (len_ - pos_ < ramp) env = 0.5f - 0.5f * cosf((float)M_PI * (len_ - pos_) / ramp);
        s = amp_ * env * sinf(phase_);
        phase_ += w_;
        if (phase_ > 2.0f * (float)M_PI) phase_ -= 2.0f * (float)M_PI;
      }
      out[k++] = (int16_t)s;
      pos_++;
      remain_--;
    }
    return k;
  }

 private:
  static const char* code(char c) {
    static const char* letters[26] = {".-",   "-...", "-.-.", "-..",  ".",   "..-.", "--.",  "....", "..",
                                      ".---", "-.-",  ".-..", "--",   "-.",  "---",  ".--.", "--.-", ".-.",
                                      "...",  "-",    "..-",  "...-", ".--", "-..-", "-.--", "--.."};
    static const char* digits[10] = {"-----", ".----", "..---", "...--", "....-",
                                     ".....", "-....", "--...", "---..", "----."};
    c = (char)toupper((unsigned char)c);
    if (c >= 'A' && c <= 'Z') return letters[c - 'A'];
    if (c >= '0' && c <= '9') return digits[c - '0'];
    if (c == '/') return "-..-.";
    return nullptr;  // space or unknown
  }
  void segment(bool on, uint32_t samples) {
    on_ = on;
    remain_ = len_ = samples;
    pos_ = 0;
  }
  // Advance to the next element. Returns false at the end.
  bool next() {
    if (gap_) {  // inter-element gap after a tone
      gap_ = false;
      segment(false, dot_);
      return true;
    }
    if (sym_ && *sym_) {
      segment(true, *sym_ == '-' ? 3 * dot_ : dot_);
      sym_++;
      gap_ = true;
      return true;
    }
    // end of a character
    if (text_[ch_] == 0) {
      if (!tailDone_) {
        tailDone_ = true;
        segment(false, tail_);  // trailing silence
        return true;
      }
      tailDone_ = false;
      return false;
    }
    char c = text_[ch_++];
    sym_ = code(c);
    if (!sym_) {  // word space (7 dots, minus the 1 already given)
      segment(false, 6 * dot_);
      return true;
    }
    if (ch_ > 1) {  // letter space (3 dots, minus the 1 already given)
      segment(false, 2 * dot_);
      return true;
    }
    return next();
  }

  char text_[32] = {0};
  const char* sym_ = nullptr;
  int ch_ = 0;
  uint32_t dot_ = 480, remain_ = 0, pos_ = 0, len_ = 0, tail_ = 1600;
  float w_ = 0, amp_ = 0, phase_ = 0;
  bool on_ = false, gap_ = false, done_ = true, tailDone_ = false;
};
