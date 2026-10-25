# Programas de Linux portados a BrysOS

BrysOS ejecuta programas de Linux x86-64 estáticos (musl) con su capa de llamadas de Linux
(`kernel/src/proc/linux.cpp`). Aquí están los scripts que compilan los que lleva el sistema.
Todo se compila en Windows con MSYS2 (ver `compilar.ps1`) y el compilador cruzado de
`E:\BrysOS-dev\cross`.

| Archivo | Para qué |
|---|---|
| `musl-gcc` | Envoltorio de `x86_64-elf-gcc` para compilar contra musl (copia en `E:\BrysOS-dev\musl\bin`) |
| `build-deps.sh` | zlib, mbedTLS, curl, libpng, libjpeg y expat → sysroot `E:\BrysOS-dev\sysroot` |
| `build-netsurf.sh` | NetSurf 3.11 (frontend framebuffer) y el árbol `E:\BrysOS-dev\bundle` |
| `netsurf/brysos.c` | Superficie "brysos" de libnsfb: ventana, lienzo, teclado y ratón de BrysOS |
| `netsurf/framebuffer-brysos.patch` | Parches del frontend: elegir la superficie, teclado con la distribución del sistema, texto UTF-8 |
| `netsurf/Choices` | Opciones de NetSurf en BrysOS (página de inicio, certificados, colores de Windows 98) |

## Fuentes (en `E:\BrysOS-dev\descargas\ports`)

- zlib 1.3.2, mbedtls 3.6.7, curl 8.23.0, libpng 1.6.59, jpegsrc.v10, expat 2.7.3
- netsurf-all-3.11.tar.gz (https://download.netsurf-browser.org/netsurf/releases/source-full/)
- cacert.pem (https://curl.se/ca/cacert.pem)

## Orden

```
MSYSTEM=UCRT64 bash -l tools/ports/build-deps.sh
MSYSTEM=MSYS   bash -l tools/ports/build-netsurf.sh
.\compilar.ps1
```

`build-netsurf.sh` deja en `E:\BrysOS-dev\bundle` una copia de `C:\` con `Archivos de programa\NetSurf.run`,
`BrysOS\Linux\usr\share\netsurf\*` y `BrysOS\Linux\etc\ssl\cert.pem`. El Makefile (variable
`BUNDLE`) mete esos archivos en el núcleo, que los escribe en el disco al instalarse o al
actualizarse. Sin la carpeta, BrysOS se compila igual, sin navegador.

Cosas que costaron: MSYS convierte las rutas de Linux de los `-D` en rutas de Windows
(`MSYS2_ARG_CONV_EXCL`), de las herramientas de NetSurf, una necesita el gcc de UCRT64 (libpng)
y otra el de MSYS (getline), y el enlace final no cabe en los 32 KB de línea de órdenes de
Windows (archivo de respuesta en `musl-gcc`).
