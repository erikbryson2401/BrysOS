# Historial de versiones

Cada versión tiene su ISO (instalación desde cero) y su paquete `.bsi` (actualización desde el
propio sistema) en la página de *Releases* de GitHub.

**BrysOS 1.0** será la primera versión final: con internet y navegador. Todas las anteriores son
**alpha** (de prueba). Se publicaron al principio con números 1.x y se renumeraron el 24/10/2026;
dentro de cada ISO antigua, `VER` sigue mostrando el número con el que se compiló.

## 1.0 — en desarrollo

- WiFi del HP dv5 con drivers de Linux (b43 y mac80211).
- Descargas en el navegador.
- Vídeo con FFmpeg y YouTube a través de Invidious.

## 0.7 alpha — 25/10/2026

El navegador.

- **Internet**: [NetSurf 3.11](https://www.netsurf-browser.org/) en el escritorio y en Inicio >
  Programas. HTML, CSS, imágenes PNG, JPEG, GIF y SVG, **HTTPS** con curl y Mbed TLS y los
  certificados raíz de Mozilla. Página de inicio: DuckDuckGo (versión HTML).
- **Programas de Linux**: BrysOS ejecuta programas de Linux x86-64 estáticos (musl): archivos,
  memoria, tiempo, sockets TCP/UDP, `poll`, tuberías, `eventfd`, aleatorios. NetSurf y curl
  son programas de Linux sin cambios en su núcleo; sólo se les ha escrito la parte que dibuja
  en una ventana de BrysOS.
- Los programas de Linux pueden abrir **ventanas de BrysOS** (`sdk/include/brysos_linux.h`),
  con la nueva **rueda del ratón** en los lienzos (`EV_RUEDA`).
- **Aleatorios para criptografía**: entropía de los tiempos de las interrupciones y ChaCha20.
- El núcleo lleva dentro el navegador y sus recursos, y los escribe en el disco al instalarse
  o actualizarse (`C:\Archivos de programa\NetSurf.run`, `C:\BrysOS\Linux`).
- Créditos y licencias: NetSurf y sus bibliotecas, musl, curl, Mbed TLS, zlib, libpng, libjpeg,
  Expat y los certificados de Mozilla.

## 0.6 alpha — 24/10/2026

Primera conexión a Internet.

- **Tarjetas de red Intel PRO/1000** con el driver `e1000` de Linux 6.12 sin modificar (QEMU y
  VirtualBox).
- **TCP/IP con lwIP 2.2.1**: DHCP, DNS, ICMP, TCP y UDP.
- Órdenes **`IPCONFIG`** (y `IPCONFIG /DNS servidor`) y **`PING`**, con la misma salida que en Windows.
- La capa de compatibilidad con Linux crece: paquetes (`sk_buff`), dispositivos de red, NAPI,
  temporizadores, colas de trabajo, tasklets e interrupciones compartidas, en el hilo
  `linux-trabajos`.
- Créditos y licencias: lwIP (BSD) y e1000 (GPLv2).

## 0.5 alpha (antes 1.3 alfa 2) — 24/10/2026

Primer paso hacia internet.

- **Capa de compatibilidad con Linux** (`kernel/src/linux/`): cabeceras que imitan las del núcleo
  Linux y su implementación sobre BrysOS, para usar drivers de Linux sin modificarlos.
- **Driver `ssb` de Linux 6.12** sin cambios: el bus interno de las tarjetas WiFi Broadcom
  (como la BCM4322 del HP Pavilion dv5).
- **Hilos del núcleo** cooperativos e **interrupciones compartidas**.
- Orden **`RED`** del Símbolo del sistema: informe de las tarjetas de red en el pendrive.
- Créditos y licencias: entrada de Linux (GPLv2).

## 0.4 alpha (antes 1.3 alfa) — 24/10/2026

- **Créditos y licencias**: ventana en el primer arranque del sistema instalado (con "No volver
  a mostrar"), también en Inicio > Ayuda y con Ejecutar > `creditos`.
- BrysOS se compila en **Windows** (MSYS2, GCC 15.2 cruzado y GRUB 2.14 propios).
- Las versiones preliminares (alfa, beta) cuentan como anteriores a la versión final en el
  Actualizador.

## 0.3 alpha (antes 1.2) — 19/10/2026

- **Programas `.run`** (como los `.exe`): en modo usuario, cada uno con su memoria y multitarea
  apropiativa. Si uno falla o se cuelga, el resto sigue (Ctrl+Alt+Supr: "Cerrar programa").
  Incluye Hola y Serpiente, y el SDK en `sdk/`.
- **Batería y brillo**: intérprete de AML propio, medidor de energía en la barra de tareas,
  avisos de batería baja y orden `ENERGIA`.
- El Reproductor de sonido abre **MP3** (minimp3).
- **Fondo de escritorio** (centrado, mosaico, expandido o ajustado).

## 0.2.1 alpha (antes 1.1.1) — 19/10/2026

- Sonido en el HP Pavilion dv5: se activan los altavoces internos (la BIOS los marca como
  desconectados) y su amplificador (GPIO0).
- Orden `SONIDO`: prueba el sonido y guarda un informe del códec.

## 0.2 alpha (antes 1.1) — 19/10/2026

- **USB 2.0** (EHCI): los pendrives van a alta velocidad, se pueden escribir y "Expulsar".
- **Volumen**: altavoz junto al reloj, teclas de volumen del portátil y Panel de control.
- **Bibliotecas del sistema (`.bsl`)**: `lenguajes.bsl`, `images.bsl` y `sonido.bsl`.
- **Visor de imágenes** (BMP, PNG, JPEG y GIF animado) y **Reproductor de sonido** (WAV).
- Orden `ACPI`: guarda las tablas del equipo en el pendrive.
- Corrige un fallo de FAT32 al crear archivos con nombre corto.
- Desde la 0.1 (1.0) hay que usar el paquete que sólo trae el núcleo (`BrysOS-0.2-alpha.bsi`).

## 0.1 alpha (antes 1.0) — 18/10/2026

Primera versión: núcleo de 64 bits, escritorio al estilo de Windows 98, Explorador, Bloc de notas,
Paint, Calculadora, Símbolo del sistema, Buscaminas, instalador con GRUB (BIOS y UEFI), FAT32,
AHCI, USB 1.1, teclado y ratón PS/2 y USB, sonido HD Audio y AC'97, y actualizaciones `.bsi`.
