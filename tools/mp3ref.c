/* Decodifica un MP3 con el mismo minimp3 de sonido.bsl y del mismo modo (salta la etiqueta
 * ID3v2, quita la ID3v1, tramas no válidas = silencio) y lo guarda como WAV estéreo de 16 bits
 * a la frecuencia del archivo: es la referencia exacta para comprobar el Reproductor.
 * Uso: mp3ref entrada.mp3 salida.wav */
#define MINIMP3_IMPLEMENTATION
#define MINIMP3_NO_SIMD
#include "minimp3.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void le32(FILE* f, unsigned v) { fputc(v & 255, f); fputc(v >> 8 & 255, f); fputc(v >> 16 & 255, f); fputc(v >> 24, f); }
static void le16(FILE* f, unsigned v) { fputc(v & 255, f); fputc(v >> 8 & 255, f); }

int main(int argc, char** argv) {
    if (argc != 3) return 2;
    FILE* f = fopen(argv[1], "rb");
    if (!f) return 1;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char* d = malloc(n);
    if (fread(d, 1, n, f) != (size_t)n) return 1;
    fclose(f);
    long skip = 0;
    if (n >= 10 && !memcmp(d, "ID3", 3))
        skip = 10 + ((d[6] & 0x7F) << 21 | (d[7] & 0x7F) << 14 | (d[8] & 0x7F) << 7 | (d[9] & 0x7F)) + ((d[5] & 0x10) ? 10 : 0);
    if (n - skip >= 128 && !memcmp(d + n - 128, "TAG", 3)) n -= 128;
    unsigned char* p = d + skip;
    long size = n - skip;

    static mp3dec_t dec;
    mp3dec_init(&dec);
    /* primero, las tramas (como sonido.bsl al abrir) */
    long* offs = malloc(sizeof(long) * (size / 24 + 16));
    int nf = 0, spf = 0, hz = 0;
    for (long pos = 0; pos < size;) {
        mp3dec_frame_info_t info;
        int s = mp3dec_decode_frame(&dec, p + pos, (int)(size - pos), NULL, &info);
        if (info.frame_bytes <= 0) break;
        if (s > 0) {
            if (!nf) { spf = s; hz = info.hz; }
            offs[nf++] = pos + info.frame_offset;
        }
        pos += info.frame_bytes;
    }
    mp3dec_init(&dec);
    FILE* o = fopen(argv[2], "wb");
    unsigned data = (unsigned)nf * spf * 4;
    fwrite("RIFF", 1, 4, o); le32(o, 36 + data); fwrite("WAVEfmt ", 1, 8, o);
    le32(o, 16); le16(o, 1); le16(o, 2); le32(o, hz); le32(o, hz * 4); le16(o, 4); le16(o, 16);
    fwrite("data", 1, 4, o); le32(o, data);
    static short pcm[MINIMP3_MAX_SAMPLES_PER_FRAME], st[MINIMP3_MAX_SAMPLES_PER_FRAME];
    for (int k = 0; k < nf; k++) {
        mp3dec_frame_info_t info;
        int s = mp3dec_decode_frame(&dec, p + offs[k], (int)(size - offs[k]), pcm, &info);
        int ch = info.channels;
        if (s <= 0 || s > 1152) { s = spf; ch = 1; memset(pcm, 0, sizeof pcm); }
        for (int i = 0; i < s; i++) {
            st[i * 2] = pcm[i * ch];
            st[i * 2 + 1] = pcm[i * ch + ch - 1];
        }
        fwrite(st, 4, s, o);
    }
    fclose(o);
    fprintf(stderr, "mp3ref: %d tramas de %d muestras, %d Hz\n", nf, spf, hz);
    return 0;
}
