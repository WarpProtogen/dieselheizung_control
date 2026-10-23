#!/usr/bin/env python3
"""Dekodiert die Saleae-Logic-2-Aufnahmen (.sal) der Bedienpanel-Leitung.

Aufruf:  python3 tools/decode_sal.py einschalten.sal [--alle]

Ohne --alle werden nur Frames ausgegeben, deren Inhalt sich geaendert hat.
"""
import bisect, io, json, struct, sys, zipfile

BAUD = 9600


def lade_kanal(sal, kanal=0):
    """Liest digital-N.bin (Logic 2, interne Version 4) und liefert (Startpegel, Flanken, Samplerate)."""
    z = zipfile.ZipFile(sal)
    meta = json.loads(z.read('meta.json'))['data']
    rate = meta['legacySettings']['sampleRate']['digital']
    d = z.read(f'digital-{kanal}.bin')
    assert d[:8] == b'<SALEAE>'
    p, flanken, start = 81, [], None
    while p + 26 <= len(d):
        s, e = struct.unpack_from('<QQ', d, p)
        pegel = d[p + 16]
        n = struct.unpack_from('<Q', d, p + 18)[0]
        x = d[p + 26:p + 26 + n]
        p += 26 + n
        if start is None:
            start = pegel
            flanken.append(s)
        t, i = s, 0
        # RLE-Eintraege: Bit7=0, Bit6=weitere Bytes folgen (Bit7 dort = weitere Bytes), Laenge = Wert+1
        while i < len(x):
            b = x[i]; i += 1
            v = b & 0x3F
            if b & 0x40:
                while True:
                    c = x[i]; i += 1
                    v = (v << 7) | (c & 0x7F)
                    if not c & 0x80:
                        break
            t += v + 1
            flanken.append(t)
        assert t == e, 'Block passt nicht'
    return start, flanken, rate


def uart(start, flanken, rate, baud=BAUD):
    """Invertiertes UART 8N1 (Ruhepegel LOW, Startbit HIGH)."""
    bit = rate / baud
    pegel = lambda t: start ^ (bisect.bisect_right(flanken, t) & 1)
    i, out = 0, []
    while i < len(flanken):
        t = flanken[i]
        if pegel(t) != 1:
            i += 1
            continue
        wert = sum((1 - pegel(t + bit * (k + 1.5))) << k for k in range(8))
        out.append((t / rate, wert))
        ende = t + bit * 9.6
        while i < len(flanken) and flanken[i] < ende:
            i += 1
    return out


def frames(bytes_):
    """Zerlegt den Bytestrom in 20-Byte-Frames FA..FB (Panel) und EA..EB (Heizung)."""
    i = 0
    while i + 20 <= len(bytes_):
        t, b0 = bytes_[i]
        b = [v for _, v in bytes_[i:i + 20]]
        if b0 in (0xFA, 0xEA) and b[19] == b0 + 1:
            x = 0
            for v in b[1:18]:
                x ^= v
            yield t, b, x == b[18]
            i += 20
        else:
            i += 1


def deute(b):
    if b[0] == 0xFA:
        return f'Befehl {b[1]:02X}'
    return (f'Befehl-Echo {b[1]:02X}  Status {b[3]:02X}  U={b[6] + b[5] / 100:5.2f}V  '
            f'T1={b[7]:3d} T2={b[12]:3d}  B11={b[11]:3d} B13={b[13]:3d} B14={b[14]:3d}')


def main():
    alle = '--alle' in sys.argv
    sal = [a for a in sys.argv[1:] if not a.startswith('--')][0]
    s, f, r = lade_kanal(sal)
    letzte = {}
    for t, b, ok in frames(uart(s, f, r)):
        if not alle and letzte.get(b[0]) == b[1:18]:
            continue
        letzte[b[0]] = b[1:18]
        print(f'{t:8.2f}s {"OK " if ok else "CRC"} ' + ' '.join(f'{v:02X}' for v in b) + '   ' + deute(b))


if __name__ == '__main__':
    main()
