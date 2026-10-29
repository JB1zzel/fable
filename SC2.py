#!/usr/bin/env python3
"""
SC2.py - interactive spell checker prototype.

Usage:
    py SC2.py dict.smd input.txt output.txt

For each unknown word:
    - shows the sentence with the word in [brackets]
    - lists 5 guesses per page, numbered 1-5
    - option 8 = more guesses (next 5)
    - option 6 = ignore, option 7 = type a replacement (re-checked)
    - q = quit and save
The output is written to <output>.tmp during the session, then replaces
<output> atomically on exit.
"""

import struct, sys, re, os

RESTART     = 8
MAX_GUESSES = 100
PAGE_SIZE   = 5
EDIT_CAP    = 5

# ----------------------------------------------------------------------
# Dictionary
# ----------------------------------------------------------------------
def load_dict(smd_path):
    d = open(smd_path, 'rb').read()
    if d[:4] != b'SMD3':
        raise SystemExit(f"{smd_path}: not an SMD3 file")
    word_count, restart_count, restart_off, data_off, data_len = \
        struct.unpack_from('<IIIII', d, 4)
    words = set()
    for r in range(restart_count):
        o = struct.unpack_from('<I', d, restart_off + 4*r)[0]
        prev = b''
        for i in range(RESTART):
            if data_off + o + 2 > len(d):
                break
            shared = d[data_off + o]
            slen   = d[data_off + o + 1]
            suffix = d[data_off + o + 2 : data_off + o + 2 + slen]
            w = prev[:shared] + suffix
            words.add(w.decode('latin-1').upper())
            prev = w
            o += 2 + slen
            if len(words) >= word_count:
                break
        if len(words) >= word_count:
            break
    return words

# ----------------------------------------------------------------------
# Damerau-Levenshtein distance (transposition = 1 edit)
# ----------------------------------------------------------------------
def edit_distance(a, b, cap=EDIT_CAP):
    if abs(len(a) - len(b)) > cap:
        return cap + 1
    la, lb = len(a), len(b)
    prev2 = None
    prev = list(range(lb + 1))
    for i in range(1, la + 1):
        cur = [i] + [0] * lb
        row_min = cur[0]
        for j in range(1, lb + 1):
            cost = 0 if a[i-1] == b[j-1] else 1
            cur[j] = min(prev[j] + 1, cur[j-1] + 1, prev[j-1] + cost)
            if (prev2 is not None and i > 1 and j > 1
                    and a[i-1] == b[j-2] and a[i-2] == b[j-1]):
                cur[j] = min(cur[j], prev2[j-2] + 1)
            if cur[j] < row_min:
                row_min = cur[j]
        if row_min > cap:
            return cap + 1
        prev2 = prev
        prev = cur
    return prev[lb]

# ----------------------------------------------------------------------
# Common prefix (for ranking guesses)
# ----------------------------------------------------------------------
def common_prefix(a, b):
    n = 0
    m = min(len(a), len(b))
    while n < m and a[n] == b[n]:
        n += 1
    return n

def guesses(word, words, n=MAX_GUESSES):
    key = word.upper()
    if not key:
        return []
    first = key[0]
    lo, hi = len(key) - 2, len(key) + 2
    cands = []
    for w in words:
        if not w or w[0] != first:
            continue
        if not (lo <= len(w) <= hi):
            continue
        d = edit_distance(key, w, cap=EDIT_CAP)
        if d <= EDIT_CAP:
            cands.append((d, w))
    cands.sort(key=lambda x: (
        x[0],
        -common_prefix(key, x[1]),
        abs(len(x[1]) - len(key)),
        x[1],
    ))
    return [w for _, w in cands[:n]]

# ----------------------------------------------------------------------
# Case handling
# ----------------------------------------------------------------------
def match_case(template, word):
    if template.isupper():
        return word.upper()
    if template.islower():
        return word.lower()
    if template[:1].isupper():
        return word.capitalize()
    return word.lower()

# ----------------------------------------------------------------------
# Text helpers
# ----------------------------------------------------------------------
WORD_RE = re.compile(r"[A-Za-z]+(?:'[A-Za-z]+)?")

def is_known(word, words):
    key = word.upper()
    if "'" in key:
        a, b = key.split("'", 1)
        return a in words and b in words
    return key in words

def sentence_around(text, start, end):
    before = max(text.rfind('.', 0, start), text.rfind('!', 0, start),
                 text.rfind('?', 0, start), text.rfind('\n', 0, start)) + 1
    after = len(text)
    for ch in '.!?\n':
        p = text.find(ch, end)
        if p != -1 and p < after:
            after = p + 1
    s = text[before:after].strip()
    ws = start - before
    we = end - before
    lead = len(text[before:after]) - len(text[before:after].lstrip())
    return s, ws - lead, we - lead

def show_sentence(s, ws, we):
    print()
    print("  " + s[:ws] + "[" + s[ws:we] + "]" + s[we:])

def save(path, text):
    open(path, 'w', encoding='utf-8').write(text)

# ----------------------------------------------------------------------
# Prompts
# ----------------------------------------------------------------------
QUIT = object()

def prompt_choice(word, gs):
    page = 0
    while True:
        start = page * PAGE_SIZE
        chunk = gs[start:start + PAGE_SIZE]
        print()
        for i, g in enumerate(chunk, 1):
            print(f"   {i}. {match_case(word, g)}")
        if start + PAGE_SIZE < len(gs):
            print(f"   6. more guesses ({len(gs) - start - PAGE_SIZE} left)")
        print(f"   7. ignore")
        print(f"   8. replace")
        print(f"   q. quit and save")
        try:
            ch = input("choose (1-5, 6=ignore, 7=replace, 8=more, q=quit): ").strip()
        except (EOFError, KeyboardInterrupt):
            return QUIT
        if ch.lower() == 'q':
            return QUIT
        if ch == '':
            return 'cancel'
        if ch == '6':
            if start + PAGE_SIZE < len(gs):
                page += 1
            continue
        if ch.isdigit():
            n = int(ch)
            if 1 <= n <= len(chunk):
                return ('pick', start + n - 1)
        if ch == '7':
            return 'ignore'
        if ch == '8':
            return 'replace'

def ask_replacement(words, original):
    while True:
        try:
            w = input(f"replace [{original}] with (q=quit, empty=cancel): ").strip()
        except (EOFError, KeyboardInterrupt):
            return QUIT
        if w.lower() == 'q':
            return QUIT
        if w == '':
            return None
        key = w.upper()
        if not all('A' <= c <= 'Z' for c in key):
            print("  letters only")
            continue
        if is_known(key, words):
            return w
        print(f"  '{w}' is not in the dictionary.")
        gs = guesses(w, words)
        page = 0
        while True:
            start = page * PAGE_SIZE
            chunk = gs[start:start + PAGE_SIZE]
            for i, g in enumerate(chunk, 1):
                print(f"   {i}. {match_case(w, g)}")
            if start + PAGE_SIZE < len(gs):
                print(f"   6. more guesses ({len(gs) - start - PAGE_SIZE} left)")
            print(f"   7. ignore (keep '{original}')")
            print(f"   8. retype")
            print(f"   q. quit and save")
            try:
                pick = input("choose (1-5, 6=more, 7=ignore, 8=retype, q=quit): ").strip().lower()
            except (EOFError, KeyboardInterrupt):
                return QUIT
            if pick == 'q':
                return QUIT
            if pick == '6':
                if start + PAGE_SIZE < len(gs):
                    page += 1
                continue
            if pick == '7':
                return None
            if pick == '8' or pick == '':
                break
            if pick.isdigit():
                n = int(pick)
                if 1 <= n <= len(chunk):
                    return match_case(w, gs[start + n - 1])
# ----------------------------------------------------------------------
# Main loop
# ----------------------------------------------------------------------
def process(text, words, out_path):
    fixes = 0
    pos = 0
    while True:
        m = WORD_RE.search(text, pos)
        if not m:
            break
        w = m.group(0)
        if is_known(w, words):
            pos = m.end()
            continue
        s, ws, we = sentence_around(text, m.start(), m.end())
        show_sentence(s, ws, we)
        gs = guesses(w, words)
        choice = prompt_choice(w, gs)
        if choice is QUIT:
            print("\nquitting, saving...")
            save(out_path, text)
            return text, fixes, True
        if choice == 'cancel' or choice == 'ignore':
            pos = m.end()
            continue
        if isinstance(choice, tuple) and choice[0] == 'pick':
            rep = match_case(w, gs[choice[1]])
        elif choice == 'replace':
            rep = ask_replacement(words, w)
            if rep is QUIT:
                print("\nquitting, saving...")
                save(out_path, text)
                return text, fixes, True
            if rep is None:
                pos = m.end()
                continue
        else:
            pos = m.end()
            continue
        text = text[:m.start()] + rep + text[m.end():]
        fixes += 1
        save(out_path, text)
        print(f"  -> replaced with '{rep}', saved.")
        pos = m.start() + len(rep)
    save(out_path, text)
    return text, fixes, False

def main(smd_path, in_path, out_path):
    print(f"loading {smd_path}...")
    words = load_dict(smd_path)
    print(f"  {len(words)} words loaded")
    text = open(in_path, encoding='utf-8', errors='replace').read()

    tmp = out_path + '.tmp'
    save(tmp, text)
    result, fixes, quit_early = process(text, words, tmp)

    os.replace(tmp, out_path)
    if quit_early:
        print(f"updated {out_path} ({fixes} replacements before quit)")
    else:
        print(f"\nupdated {out_path} ({fixes} replacements)")

if __name__ == '__main__':
    if len(sys.argv) != 4:
        print(__doc__); sys.exit(1)
    main(sys.argv[1], sys.argv[2], sys.argv[3])