#!/usr/bin/env python3
"""
read_smd4.py - load an SMD4 dictionary and list every accepted surface form.
Usage: py read_smd4.py dict.smd4 [--section XY]
"""
import sys, struct

BARE_CLASSES = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10}

def load(path):
    b = open(path, "rb").read()
    assert b[:4] == b"SMD4", "not SMD4"
    (n_stems, n_sec, tok_off, cls_off, sec_off, data_off,
     data_len, _, _) = struct.unpack_from("<IIIIIIIII", b, 4)

    # tokens
    p = tok_off
    tokens = []
    for _ in range(128):
        n = b[p]; p += 1
        tokens.append(b[p:p+n]); p += n

    # classes
    p = cls_off
    ncls = b[p]; p += 1
    classes = []
    for _ in range(ncls):
        ne = b[p]; p += 1
        ends = []
        for _ in range(ne):
            L = b[p]; p += 1
            ends.append(b[p:p+L]); p += L
        classes.append(ends)

    # sections
    p = sec_off
    sections = []
    for _ in range(n_sec):
        key = b[p:p+2].decode("ascii"); p += 2
        doff, dcnt = struct.unpack_from("<II", b, p); p += 8
        sections.append((key, doff, dcnt))

    return b, tokens, classes, sections, data_off, data_len

def decode_body(body, tokens):
    out = bytearray()
    for x in body:
        if x >= 0x80:
            out += tokens[x & 0x7F]
        else:
            out.append(x)
    return bytes(out)

def iter_stems(b, tokens, classes, sections, data_off, data_len, only=None):
    for key, doff, dcnt in sections:
        if only and key != only: continue
        pos = doff
        end = doff + dcnt
        prev = b""
        while pos < end:
            head = b[pos]; pos += 1
            plen = head & 0x0F
            flags = head >> 4
            blen = b[pos]; pos += 1
            body = b[pos:pos+blen]; pos += blen
            has_class = bool(flags & 2)
            cls = b[pos] if has_class else 0
            if has_class: pos += 1
            stem = prev[:plen] + decode_body(body, tokens)
            prev = stem
            yield stem, cls, flags

def forms_of(stem, cls, flags, classes):
    sb = stem
    out = []
    if (flags & 1) or cls in BARE_CLASSES:
        out.append(sb)
    for e in classes[cls]:
        if e:  # empty ending already handled by bare case
            out.append(sb + e)
    return out

def main(argv):
    if len(argv) < 2:
        print(__doc__); return 1
    path = argv[1]
    only = None
    if len(argv) >= 4 and argv[2] == "--section":
        only = argv[3].upper()
    b, tokens, classes, sections, data_off, data_len = load(path)
    seen = set()
    for stem, cls, flags in iter_stems(b, tokens, classes, sections,
                                       data_off, data_len, only):
        for f in forms_of(stem, cls, flags, classes):
            if f not in seen:
                seen.add(f)
                print(f.decode("ascii"))
    print(f"# {len(seen)} distinct forms", file=sys.stderr)
    return 0

if __name__ == "__main__":
    sys.exit(main(sys.argv))