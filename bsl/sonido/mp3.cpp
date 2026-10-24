// Decodificador MP3 (minimp3). Este archivo se compila con SSE: ver mp3.h.
#define MINIMP3_IMPLEMENTATION
#define MINIMP3_NO_SIMD
#include "minimp3.h"
#include "mp3.h"

static_assert(sizeof(mp3dec_t) <= MP3_DECODER_BYTES, "mp3dec_t no cabe");

void mp3_reset(void* dec) { mp3dec_init((mp3dec_t*)dec); }

int mp3_frame(void* dec, const uint8_t* p, int n, int16_t* pcm, Mp3Frame& f) {
    mp3dec_frame_info_t info{};
    int r = mp3dec_decode_frame((mp3dec_t*)dec, p, n, pcm, &info);
    f.bytes = info.frame_bytes;
    f.offset = info.frame_offset;
    f.channels = info.channels;
    f.hz = info.hz;
    f.layer = info.layer;
    f.kbps = info.bitrate_kbps;
    return r;
}
