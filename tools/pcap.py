#!/usr/bin/env python3
"""Resume un .pcap de QEMU (filter-dump): una línea por paquete con Ethernet, ARP, IPv4,
ICMP, UDP (y DNS) y TCP. Para depurar la red sin Wireshark. Uso: pcap.py archivo.pcap"""
import struct, sys

data = open(sys.argv[1], "rb").read()
off = 24
n = 0
while off + 16 <= len(data):
    ts, tus, incl, orig = struct.unpack_from("<IIII", data, off)
    off += 16
    pkt = data[off:off + incl]
    off += incl
    n += 1
    if len(pkt) < 14:
        continue
    dst, src, et = pkt[0:6].hex(":"), pkt[6:12].hex(":"), struct.unpack_from(">H", pkt, 12)[0]
    line = f"{n:4} {src} -> {dst} "
    if et == 0x0806:
        op = struct.unpack_from(">H", pkt, 20)[0]
        line += f"ARP {'pregunta' if op == 1 else 'respuesta'} {'.'.join(map(str, pkt[38:42]))} (desde {'.'.join(map(str, pkt[28:32]))})"
    elif et == 0x0800:
        ip = pkt[14:]
        hl = (ip[0] & 15) * 4
        proto = ip[9]
        s, d = ".".join(map(str, ip[12:16])), ".".join(map(str, ip[16:20]))
        line += f"IP {s} -> {d} "
        l4 = ip[hl:]
        if proto == 1:
            line += f"ICMP tipo {l4[0]}"
        elif proto == 17:
            sp, dp, ln, ck = struct.unpack_from(">HHHH", l4, 0)
            line += f"UDP {sp} -> {dp} ({ln - 8} bytes, suma {ck:04x})"
            if 53 in (sp, dp):
                q = l4[8:]
                ident, flags, qd, an = struct.unpack_from(">HHHH", q, 0)
                line += f" DNS id {ident:04x} {'respuesta' if flags & 0x8000 else 'pregunta'} rcode {flags & 15} respuestas {an}"
        elif proto == 6:
            sp, dp, seq, ack, hlf = struct.unpack_from(">HHIIH", l4, 0)
            flags = "".join(c for c, b in zip("FSRPAU", [1, 2, 4, 8, 16, 32]) if hlf & b)
            line += f"TCP {sp} -> {dp} [{flags}] seq {seq} ack {ack}"
        else:
            line += f"proto {proto}"
    else:
        line += f"tipo {et:04x}"
    print(line)

# Wireshark de bolsillo: cuando no puedes instalar nada, struct.unpack es tu mejor amigo. — Claude
