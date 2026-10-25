#!/bin/sh
# Compila las bibliotecas de terceros que necesitan los programas de Linux de BrysOS (el
# navegador NetSurf, sobre todo), como bibliotecas estáticas para x86_64-linux-musl, y las
# instala en el sysroot. Se ejecuta en MSYS2 (UCRT64). Uso: build-deps.sh [zlib mbedtls ...]
#   DESC: carpeta con los .tar descargados      SRC: donde se descomprimen
#   SYS:  sysroot de destino                    (ver tools/ports/README.md)
set -e
DESC=${DESC:-/e/BrysOS-dev/descargas/ports}
SRC=${SRC:-/e/BrysOS-dev/src/ports}
SYS=${SYS:-/e/BrysOS-dev/sysroot}
export PATH=/e/BrysOS-dev/musl/bin:/e/BrysOS-dev/cross/bin:$PATH
HOST=x86_64-linux-musl
export CC=$HOST-gcc AR=$HOST-ar RANLIB=$HOST-ranlib STRIP=$HOST-strip NM=$HOST-nm
J=-j12
mkdir -p "$SRC" "$SYS/include" "$SYS/lib"
log() { echo "== $*"; }

unpack() {   # unpack archivo carpeta
    [ -d "$SRC/$2" ] || tar xf "$DESC/$1" -C "$SRC"
    cd "$SRC/$2"
}

zlib() {
    unpack zlib-1.3.2.tar.gz zlib-1.3.2
    CHOST=$HOST ./configure --static --prefix="$SYS" >../zlib.log 2>&1
    make $J >>../zlib.log 2>&1 && make install >>../zlib.log 2>&1
}

mbedtls() {
    unpack mbedtls-3.6.7.tar.bz2 mbedtls-3.6.7
    # el Makefile de mbedTLS cambia de comportamiento si ve OS=Windows_NT
    env -u OS make $J lib CC=$CC AR=$AR CFLAGS="-O2" >../mbedtls.log 2>&1
    cp -r include/mbedtls include/psa "$SYS/include/"
    cp library/libmbedtls.a library/libmbedx509.a library/libmbedcrypto.a "$SYS/lib/"
}

curl_() {
    unpack curl-8.23.0.tar.xz curl-8.23.0
    ./configure --host=$HOST --prefix="$SYS" --disable-shared --enable-static \
        --with-mbedtls="$SYS" --with-zlib="$SYS" --with-ca-bundle=/etc/ssl/cert.pem \
        --disable-threaded-resolver --without-libpsl --without-zstd --without-brotli --without-nghttp2 \
        --without-libidn2 --disable-ldap --disable-rtsp --disable-dict --disable-telnet --disable-tftp \
        --disable-pop3 --disable-imap --disable-smtp --disable-gopher --disable-mqtt --disable-smb \
        --disable-manual --disable-docs --disable-ntlm --disable-kerberos-auth >../curl.log 2>&1
    make $J >>../curl.log 2>&1 && make install >>../curl.log 2>&1
}

libpng() {
    unpack libpng-1.6.59.tar.xz libpng-1.6.59
    ./configure --host=$HOST --prefix="$SYS" --disable-shared CPPFLAGS="-I$SYS/include" LDFLAGS="-L$SYS/lib" \
        >../libpng.log 2>&1
    make $J >>../libpng.log 2>&1 && make install >>../libpng.log 2>&1
}

libjpeg() {
    unpack jpegsrc.v10.tar.gz jpeg-10
    ./configure --host=$HOST --prefix="$SYS" --disable-shared >../libjpeg.log 2>&1
    make $J >>../libjpeg.log 2>&1 && make install >>../libjpeg.log 2>&1
}

expat() {
    unpack expat-2.7.3.tar.xz expat-2.7.3
    ./configure --host=$HOST --prefix="$SYS" --disable-shared --without-docbook --without-examples \
        --without-tests --without-xmlwf >../expat.log 2>&1
    make $J >>../expat.log 2>&1 && make install >>../expat.log 2>&1
}

for p in ${*:-zlib mbedtls curl_ libpng libjpeg expat}; do
    log "$p"
    ( $p ) || { echo "Error: $p (mire $SRC/${p%_}.log)"; exit 1; }
done
echo DONE
