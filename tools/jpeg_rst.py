"""Codificador JPEG secuencial mínimo (sólo para pruebas) que inserta marcadores de reinicio
(DRI/RSTn) cada 'interval' MCU. 4:4:4 o gris, tablas de Huffman de longitud fija."""
import math, struct

QY = [16, 11, 10, 16, 24, 40, 51, 61, 12, 12, 14, 19, 26, 58, 60, 55, 14, 13, 16, 24, 40, 57, 69, 56,
      14, 17, 22, 29, 51, 87, 80, 62, 18, 22, 37, 56, 68, 109, 103, 77, 24, 35, 55, 64, 81, 104, 113, 92,
      49, 64, 78, 87, 103, 121, 120, 101, 72, 92, 95, 98, 112, 100, 103, 99]
ZZ = [0, 1, 8, 16, 9, 2, 3, 10, 17, 24, 32, 25, 18, 11, 4, 5, 12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6, 7, 14, 21,
      28, 35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51, 58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61,
      54, 47, 55, 62, 63]
C = [[(math.sqrt(0.5) if u == 0 else 1) * math.cos((2 * x + 1) * u * math.pi / 16) / 2 for x in range(8)] for u in range(8)]


def fdct(b):
    tmp = [[sum(C[u][x] * b[y * 8 + x] for x in range(8)) for u in range(8)] for y in range(8)]
    return [sum(C[v][y] * tmp[y][u] for y in range(8)) for v in range(8) for u in range(8)]


def category(v):
    v = abs(v)
    n = 0
    while v:
        n += 1
        v >>= 1
    return n


def encode(img, path, interval=1, gray=False):
    w, h = img.size
    planes = [img.convert("L").load()] if gray else [c.load() for c in img.convert("YCbCr").split()]
    nc = len(planes)
    mcux, mcuy = (w + 7) // 8, (h + 7) // 8
    # coeficientes cuantificados de cada bloque
    blocks = []
    for my in range(mcuy):
        for mx in range(mcux):
            mcu = []
            for p in planes:
                b = [p[min(mx * 8 + x, w - 1), min(my * 8 + y, h - 1)] - 128 for y in range(8) for x in range(8)]
                f = fdct(b)
                mcu.append([int(round(f[ZZ[k]] / QY[ZZ[k]])) for k in range(64)])
            blocks.append(mcu)
    # símbolos (para tablas de longitud fija: DC 4 bits, AC 8 bits)
    ac_syms = set()
    for mcu in blocks:
        for q in mcu:
            run = 0
            for k in range(1, 64):
                if q[k] == 0:
                    run += 1
                    continue
                while run > 15:
                    ac_syms.add(0xF0)
                    run -= 16
                ac_syms.add((run << 4) | category(q[k]))
                run = 0
            if run:
                ac_syms.add(0)
    ac_syms = sorted(ac_syms)
    ac_code = {s: i for i, s in enumerate(ac_syms)}
    out = bytearray()
    buf, nbits = 0, 0

    def put(v, n):
        nonlocal buf, nbits
        for i in range(n - 1, -1, -1):
            buf = (buf << 1) | ((v >> i) & 1)
            nbits += 1
            if nbits == 8:
                out.append(buf)
                if buf == 0xFF:
                    out.append(0)
                buf, nbits = 0, 0

    def flush():
        nonlocal buf, nbits
        while nbits:
            put(1, 1)

    pred = [0] * nc
    for i, mcu in enumerate(blocks):
        if interval and i and i % interval == 0:
            flush()
            out.extend(b"\xFF" + bytes([0xD0 + ((i // interval - 1) & 7)]))
            pred = [0] * nc
        for c, q in enumerate(mcu):
            diff = q[0] - pred[c]
            pred[c] = q[0]
            s = category(diff)
            put(s, 4)
            if s:
                put(diff if diff > 0 else diff + (1 << s) - 1, s)
            run = 0
            for k in range(1, 64):
                if q[k] == 0:
                    run += 1
                    continue
                while run > 15:
                    put(ac_code[0xF0], 8)
                    run -= 16
                s = category(q[k])
                put(ac_code[(run << 4) | s], 8)
                put(q[k] if q[k] > 0 else q[k] + (1 << s) - 1, s)
                run = 0
            if run:
                put(ac_code[0], 8)
    flush()

    def seg(m, body):
        return b"\xFF" + bytes([m]) + struct.pack(">H", len(body) + 2) + body

    data = b"\xFF\xD8"
    data += seg(0xDB, bytes([0]) + bytes(QY[ZZ[k]] for k in range(64)))
    data += seg(0xC0, struct.pack(">BHHB", 8, h, w, nc) + b"".join(bytes([i + 1, 0x11, 0]) for i in range(nc)))
    dc_counts = [0] * 16
    dc_counts[3] = 12   # 12 categorías con códigos de 4 bits
    data += seg(0xC4, bytes([0x00]) + bytes(dc_counts) + bytes(range(12)))
    ac_counts = [0] * 16
    ac_counts[7] = len(ac_syms)
    data += seg(0xC4, bytes([0x10]) + bytes(ac_counts) + bytes(ac_syms))
    if interval:
        data += seg(0xDD, struct.pack(">H", interval))
    data += seg(0xDA, bytes([nc]) + b"".join(bytes([i + 1, 0x00]) for i in range(nc)) + bytes([0, 63, 0]))
    data += bytes(out) + b"\xFF\xD9"
    open(path, "wb").write(data)
