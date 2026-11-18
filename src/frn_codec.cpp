#include "frn_codec.h"

#include <string.h>

extern "C" {
#include "gsm.h"
}

namespace frn {

static gsm makeWav49() {
  gsm g = gsm_create();
  if (g) {
    int one = 1;
    gsm_option(g, GSM_OPT_WAV49, &one);
  }
  return g;
}

Codec::Codec() : enc_(makeWav49()), dec_(makeWav49()) {}

Codec::~Codec() {
  if (enc_) gsm_destroy(static_cast<gsm>(enc_));
  if (dec_) gsm_destroy(static_cast<gsm>(dec_));
}

void Codec::resetEncoder() {
  if (enc_) gsm_destroy(static_cast<gsm>(enc_));
  enc_ = makeWav49();
}

void Codec::resetDecoder() {
  if (dec_) gsm_destroy(static_cast<gsm>(dec_));
  dec_ = makeWav49();
}

void Codec::encodePacket(const int16_t* pcm, uint8_t* out) {
  gsm g = static_cast<gsm>(enc_);
  // gsm_encode takes a non-const pointer but does not modify the input.
  int16_t frame[GSM_SAMPLES];
  for (int b = 0; b < BLOCKS_PER_PACKET; b++) {
    const int16_t* src = pcm + b * BLOCK_SAMPLES;
    uint8_t* dst = out + b * BLOCK_BYTES;
    memcpy(frame, src, sizeof(frame));
    gsm_encode(g, frame, dst);             // writes 32 bytes
    memcpy(frame, src + GSM_SAMPLES, sizeof(frame));
    gsm_encode(g, frame, dst + 32);        // writes 33 bytes
  }
}

bool Codec::decodePacket(const uint8_t* in, int16_t* pcm) {
  gsm g = static_cast<gsm>(dec_);
  bool ok = true;
  uint8_t block[BLOCK_BYTES];
  for (int b = 0; b < BLOCKS_PER_PACKET; b++) {
    memcpy(block, in + b * BLOCK_BYTES, BLOCK_BYTES);
    int16_t* dst = pcm + b * BLOCK_SAMPLES;
    if (gsm_decode(g, block, dst) < 0) ok = false;                     // reads 33
    if (gsm_decode(g, block + 33, dst + GSM_SAMPLES) < 0) ok = false;  // reads 32
  }
  return ok;
}

}  // namespace frn
