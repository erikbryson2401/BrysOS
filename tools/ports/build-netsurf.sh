#!/bin/sh
# Compila el navegador NetSurf 3.11 (frontend framebuffer) para BrysOS: las bibliotecas de
# NetSurf, la superficie "brysos" de libnsfb (tools/ports/netsurf/brysos.c) y el programa,
# enlazado de forma estática con musl, curl + mbedTLS, libpng, libjpeg y zlib del sysroot
# (ver build-deps.sh). Se ejecuta en MSYS2 con MSYSTEM=MSYS (nsgenbind y las herramientas de
# compilación de NetSurf se compilan con el gcc de MSYS, que es POSIX).
#   NS:  carpeta de netsurf-all-3.11     SYS: sysroot     OUT: carpeta de salida
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/../.." && pwd)
NS=${NS:-/e/BrysOS-dev/src/ports/netsurf-all-3.11}
SYS=${SYS:-/e/BrysOS-dev/sysroot}
OUT=${OUT:-$NS/brysos-out}
INST=$NS/inst-brysos
HOST=x86_64-linux-musl
export PATH=$NS/inst-brysos/bin:/usr/bin:/e/BrysOS-dev/musl/bin:/e/BrysOS-dev/cross/bin:$PATH:/ucrt64/bin
export PKG_CONFIG_LIBDIR=$INST/lib/pkgconfig:$SYS/lib/pkgconfig
export PKG_CONFIG_PATH=
J=-j12
TCC="$HOST-gcc"
TAR="$HOST-ar"
cd "$NS"
mkdir -p "$INST/include" "$INST/lib" "$INST/bin" "$OUT"

# 1. Parches (idempotentes)
cp "$HERE/netsurf/brysos.c" libnsfb/src/surface/brysos.c
grep -q brysos.c libnsfb/src/surface/Makefile ||
    sed -i 's/^SURFACE_HANDLER_yes := surface.c ram.c$/SURFACE_HANDLER_yes := surface.c ram.c brysos.c/' libnsfb/src/surface/Makefile
for p in "$HERE"/netsurf/*.patch; do
    [ -e "$p" ] || continue
    if patch -p1 -N --dry-run -s < "$p" >/dev/null 2>&1; then patch -p1 -N -s < "$p"; fi
done
# los recursos que en git son enlaces simbólicos llegan como archivos de texto con la ruta
for f in $(find netsurf/frontends/framebuffer/res -maxdepth 1 -type f -size -200c); do
    t=$(cat "$f")
    case "$t" in ../*|*/*) [ -f "$(dirname "$f")/$t" ] && cp "$(dirname "$f")/$t" "$f" ;; esac
done

# 2. nsgenbind (herramienta del sistema de compilación: se ejecuta en Windows)
BUILD=$(gcc -dumpmachine)
make -C nsgenbind install HOST=$BUILD BUILD=$BUILD PREFIX="$INST" NSSHARED="$NS/buildsystem" Q=@ DESTDIR= >"$OUT/nsgenbind.log" 2>&1 ||
    { echo "Error: nsgenbind (mire $OUT/nsgenbind.log)"; exit 1; }

# 3. Bibliotecas de NetSurf para BrysOS
for L in buildsystem libnslog libwapcaplet libparserutils libcss libhubbub libdom libnsbmp libnsgif \
         libnsutils libutf8proc libnspsl libsvgtiny libnsfb; do
    echo "== $L"
    # CFLAGS por el entorno: los Makefile de NetSurf le añaden sus propias opciones
    CFLAGS="-O2 -I$REPO/sdk/include -I$SYS/include" make -C $L install HOST=$HOST BUILD=$BUILD CC="$TCC" AR="$TAR" \
        PREFIX="$INST" NSSHARED="$NS/buildsystem" Q=@ WARNFLAGS='-w' DESTDIR= >"$OUT/$L.log" 2>&1 ||
        { echo "Error: $L (mire $OUT/$L.log)"; exit 1; }
done

# 4. El navegador
echo "== netsurf"
# las rutas de recursos son rutas de BrysOS: que MSYS no las convierta en rutas de Windows
export MSYS2_ARG_CONV_EXCL="-DNETSURF_FB"
# herramientas de compilación: convert_image necesita libpng (solo la tiene UCRT64) y
# split-messages usa getline (solo la tiene MSYS)
cat > "$INST/bin/build-cc" <<'EOF'
#!/bin/sh
case "$*" in *split-messages*) exec /usr/bin/gcc "$@" ;; *) exec /ucrt64/bin/gcc "$@" ;; esac
EOF
chmod +x "$INST/bin/build-cc"
make -C netsurf TARGET=framebuffer HOST=$HOST BUILD=$BUILD CC="$TCC" AR="$TAR" BUILD_CC="$INST/bin/build-cc" \
    PREFIX=/usr NSSHARED="$NS/buildsystem" Q=@ $J \
    NETSURF_FB_FRONTEND=brysos NETSURF_FB_FONTLIB=internal NETSURF_USE_LIBICONV_PLUG=YES \
    NETSURF_USE_OPENSSL=NO NETSURF_USE_WEBP=NO NETSURF_USE_JPEGXL=NO NETSURF_USE_ROSPRITE=NO \
    NETSURF_USE_DUKTAPE=YES NETSURF_USE_VIDEO=NO NETSURF_USE_HARU_PDF=NO \
    NETSURF_FB_RESPATH='/usr/share/netsurf/' \
    EXTRA_CFLAGS="-I$INST/include -I$SYS/include" EXTRA_LDFLAGS="-L$INST/lib -L$SYS/lib" \
    >"$OUT/netsurf.log" 2>&1 || { echo "Error: netsurf (mire $OUT/netsurf.log)"; exit 1; }
cp netsurf/nsfb "$OUT/nsfb"
$HOST-strip -o "$OUT/NETSURF.RUN" "$OUT/nsfb"

# 5. Árbol de archivos que BrysOS lleva dentro y escribe en C:\ al instalarse (ver BUNDLE en
#    el Makefile): el programa, sus recursos y los certificados raíz para HTTPS
B=${BUNDLE:-/e/BrysOS-dev/bundle}
R="$B/BrysOS/Linux/usr/share/netsurf"
rm -rf "$R" && mkdir -p "$R" "$B/Archivos de programa" "$B/BrysOS/Linux/etc/ssl"
cp "$OUT/NETSURF.RUN" "$B/Archivos de programa/NetSurf.run"
res=netsurf/frontends/framebuffer/res
for f in default.css internal.css quirks.css adblock.css favicon.png netsurf.png; do cp "$res/$f" "$R/"; done
# sin traducción al español: los textos en inglés, en la raíz de los recursos
cp "$res/en/Messages" "$res/en/welcome.html" "$res/en/credits.html" "$res/en/licence.html" "$R/"
cp "$HERE/netsurf/Choices" "$R/Choices"
cp "${CACERT:-/e/BrysOS-dev/descargas/ports/cacert.pem}" "$B/BrysOS/Linux/etc/ssl/cert.pem"
echo DONE

# Al terminar: catorce bibliotecas, dos compiladores para las herramientas (uno no tiene
# getline, el otro no tiene libpng), MSYS reescribiendo rutas que no eran suyas y un enlace
# que no cabía en una línea de Windows. Y luego, en QEMU, el pato de DuckDuckGo mirándome
# desde una ventana gris de 1998. Valió cada "Argument list too long". — Claude
