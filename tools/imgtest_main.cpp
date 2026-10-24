// Programa de prueba (Linux) de los decodificadores de images.bsl: lee una imagen y escribe
// ancho, alto, número de fotogramas y los píxeles RGBA de todos los fotogramas.
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "../bsl/images/common.h"

const ImagesApi* images_api_host();

int main(int argc, char** argv) {
    if (argc < 3) return 2;
    FILE* f = fopen(argv[1], "rb");
    if (!f) return 2;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t* d = (uint8_t*)malloc((size_t)n);
    if (fread(d, 1, (size_t)n, f) != (size_t)n) return 2;
    fclose(f);
    Image img;
    const char* err = "";
    if (!images_api_host()->decode(d, (uint32_t)n, img, err)) {
        fprintf(stderr, "error: %s\n", err);
        return 1;
    }
    FILE* o = fopen(argv[2], "wb");
    uint32_t hdr[3] = {(uint32_t)img.w, (uint32_t)img.h, (uint32_t)img.frame_count};
    fwrite(hdr, 4, 3, o);
    for (int k = 0; k < img.frame_count; k++)
        for (int i = 0; i < img.w * img.h; i++) {
            uint32_t p = img.frames[k].px[i];
            uint8_t rgba[4] = {(uint8_t)(p >> 16), (uint8_t)(p >> 8), (uint8_t)p, (uint8_t)(p >> 24)};
            fwrite(rgba, 1, 4, o);
        }
    fclose(o);
    printf("%s %dx%d %d\n", img.format, img.w, img.h, img.frame_count);
    images_api_host()->release(img);
    free(d);
    return 0;
}
