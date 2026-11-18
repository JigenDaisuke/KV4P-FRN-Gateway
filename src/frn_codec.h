// FRN voice codec: GSM 06.10 in WAV49 ("MS-GSM") framing.
//
// One FRN voice packet = 325 bytes = 5 x 65-byte WAV49 blocks.
// One 65-byte block holds 2 GSM frames of 160 samples, so one packet
// carries 1600 samples @ 8 kHz = 200 ms of audio.
//
// libgsm in WAV49 mode writes frames of 32,33,32,33... bytes when encoding
// and reads frames of 33,32,33,32... bytes when decoding (the odd half-byte
// is carried across). That is why the second half is at +32 when encoding
// and at +33 when decoding. Same behaviour as SvxLink's ModuleFrn.
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace frn {

constexpr int SAMPLE_RATE        = 8000;
constexpr int GSM_SAMPLES        = 160;               // samples per GSM frame
constexpr int BLOCK_SAMPLES      = 2 * GSM_SAMPLES;   // 320 samples per 65-byte block
constexpr int BLOCK_BYTES        = 65;
constexpr int BLOCKS_PER_PACKET  = 5;
constexpr int PACKET_SAMPLES     = BLOCKS_PER_PACKET * BLOCK_SAMPLES;  // 1600
constexpr int PACKET_BYTES       = BLOCKS_PER_PACKET * BLOCK_BYTES;    // 325
constexpr int PACKET_MS          = PACKET_SAMPLES * 1000 / SAMPLE_RATE; // 200

class Codec {
 public:
  Codec();
  ~Codec();
  Codec(const Codec&) = delete;
  Codec& operator=(const Codec&) = delete;

  bool ok() const { return enc_ != nullptr && dec_ != nullptr; }

  // pcm: PACKET_SAMPLES samples. out: PACKET_BYTES bytes.
  void encodePacket(const int16_t* pcm, uint8_t* out);

  // in: PACKET_BYTES bytes. pcm: PACKET_SAMPLES samples. Returns false if
  // libgsm flagged a bad frame (output is still filled).
  bool decodePacket(const uint8_t* in, int16_t* pcm);

  // Reset codec state (call at start of each over).
  void resetEncoder();
  void resetDecoder();

 private:
  void* enc_;  // gsm handle (opaque to keep gsm.h out of the header)
  void* dec_;
};

}  // namespace frn
