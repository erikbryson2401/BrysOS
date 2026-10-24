// images.bsl: punto de entrada. Reconoce el formato por su firma (no por la extensión).
#include "common.h"
#ifndef IMG_HOST
#include "bsl/bsl.h"
#endif

namespace {
bool decode(const uint8_t* d, uint32_t n, Image& out, const char*& err) {
    out = Image();
    err = "";
    if (!d || n < 4) { err = T("El archivo está vacío o no es una imagen."); return false; }
    if (n >= 8 && d[0] == 0x89 && d[1] == 'P' && d[2] == 'N' && d[3] == 'G') return img::decode_png(d, n, out, err);
    if (d[0] == 0xFF && d[1] == 0xD8) return img::decode_jpeg(d, n, out, err);
    if (!memcmp(d, "GIF8", 4)) return img::decode_gif(d, n, out, err);
    if (d[0] == 'B' && d[1] == 'M') return img::decode_bmp(d, n, out, err);
    err = T("El formato de este archivo no está admitido (se admiten BMP, PNG, JPEG y GIF).");
    return false;
}

void release(Image& img) { img::free_image(img); }

const char* EXTS = "BMP;DIB;PNG;JPG;JPEG;JPE;JFIF;GIF";

bool can_open(const char* ext) {
    if (!ext || !ext[0]) return false;
    const char* p = EXTS;
    while (*p) {
        const char* e = p;
        while (*e && *e != ';') e++;
        size_t len = (size_t)(e - p), k = 0;
        while (k < len && ext[k] && (ext[k] == p[k] || ext[k] == p[k] + 32)) k++;
        if (k == len && !ext[k]) return true;
        p = *e ? e + 1 : e;
    }
    return false;
}

const char* extensions() { return EXTS; }

const ImagesApi api_v1 = {decode, release, can_open, extensions};
}

#ifdef IMG_HOST
const ImagesApi* images_api_host() { return &api_v1; }
#else
static const void* get_api(uint32_t v) { return v == IMAGES_API ? &api_v1 : nullptr; }
BSL_DEFINE("images", "1.0", "Imágenes BMP, PNG, JPEG (también progresivo) y GIF animado", nullptr, get_api);
#endif
