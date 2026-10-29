#!/usr/bin/env python3
"""
make_smd3.py - build an SMD3 file with a restart table (no full index).

Usage:
    py make_smd3.py words.txt dict.smd
"""

import struct, sys

RESTART = 8
MAGIC   = b"SMD3"

def load_words(path):
    seen = set()
    with open(path, encoding='utf-8', errors='ignore') as f:
        for line in f:
            w = line.strip().upper()
            if w and all('A' <= c <= 'Z' for c in w):
                seen.add(w)
    return sorted(seen)

def common_prefix(a, b):
    n = 0
    m = min(len(a), len(b))
    while n < m and a[n] == b[n]:
        n += 1
    return n

def encode(words):
    data = bytearray()
    restarts = []
    prev = b""
    total_shared = 0
    for i, w in enumerate(words):
        wb = w.encode('ascii')
        if i % RESTART == 0:
            shared = 0
            restarts.append(len(data))
        else:
            shared = common_prefix(prev, wb)
            if shared > 255:
                shared = 255
            total_shared += shared
        suffix = wb[shared:]
        if len(suffix) > 255:
            prev = wb
            continue
        data.append(shared)
        data.append(len(suffix))
        data += suffix
        prev = wb
    avg_shared = total_shared / max(1, len(words) - 1)
    return data, restarts, avg_shared

def build(in_path, out_path):
    words = load_words(in_path)
    print(f"words: {len(words)}")
    data, restarts, avg_shared = encode(words)
    hdr_len     = 0x20
    restart_off = hdr_len
    data_off    = restart_off + 4 * len(restarts)
    out = bytearray()
    out += MAGIC
    out += struct.pack('<IIIII', len(words), len(restarts),
                       restart_off, data_off, len(data))
    out += b'\x00' * (hdr_len - len(out))
    for r in restarts:
        out += struct.pack('<I', r)
    out += data
    open(out_path, 'wb').write(out)
    print(f"wrote {out_path}: {len(out)} bytes")
    print(f"  words    : {len(words)}")
    print(f"  restarts : {len(restarts)} @ ${restart_off:04X}")
    print(f"  data     : {len(data)} bytes @ ${data_off:04X}")
    print(f"  avg shared prefix: {avg_shared:.2f} bytes")
    print(f"  bytes/word: {len(out)/max(1,len(words)):.2f}")

if __name__ == '__main__':
    if len(sys.argv) != 3:
        print(__doc__); sys.exit(1)
    build(sys.argv[1], sys.argv[2])