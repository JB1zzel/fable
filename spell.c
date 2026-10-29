/* spell.c - spell checker for Sinclair QL (QDOS, C68/xtc68) and Windows
 * Port of SC2.py.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#ifdef QDOS
#include <qdos.h>
#else
#ifdef _WIN32
#include <windows.h>
#endif
#endif

#define RESTART      8
#define MAX_GUESSES  95
#define PAGE_SIZE    5
#define EDIT_CAP     5
#define MAX_WORD     128
#define SCRATCH_CAP  4096

/* ---------- little-endian helpers ---------- */
static unsigned long rd32(const unsigned char *p) {
    return (unsigned long)p[0] | ((unsigned long)p[1] << 8) |
           ((unsigned long)p[2] << 16) | ((unsigned long)p[3] << 24);
}

/* ---------- dictionary ---------- */
static unsigned char *dict = NULL;
static unsigned long  dict_len = 0;
static unsigned long  word_count, restart_count, restart_off, data_off;

static int load_dict(const char *path) {
    FILE *f = fopen(path, "rb");
    long  n;
    if (!f) return 0;
    fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
    if (n < 0x20) { fclose(f); return 0; }
    dict = (unsigned char *)malloc((size_t)n);
    if (!dict) { fclose(f); return 0; }
    if (fread(dict, 1, (size_t)n, f) != (size_t)n) { fclose(f); return 0; }
    fclose(f);
    if (memcmp(dict, "SMD3", 4) != 0) return 0;
    dict_len      = (unsigned long)n;
    word_count    = rd32(dict + 0x04);
    restart_count = rd32(dict + 0x08);
    restart_off   = rd32(dict + 0x0C);
    data_off      = rd32(dict + 0x10);
    if (restart_off >= dict_len || data_off >= dict_len) return 0;
    return 1;
}

static void decode_restart(unsigned long r, char *out) {
    unsigned long o, base;
    unsigned char slen;
    if (r >= restart_count) { out[0] = 0; return; }
    base = restart_off + 4*r;
    if (base + 4 > dict_len) { out[0] = 0; return; }
    o = rd32(dict + base);
    if (data_off + o + 2 > dict_len) { out[0] = 0; return; }
    slen = dict[data_off + o + 1];
    if (slen >= MAX_WORD) slen = MAX_WORD - 1;
    if (data_off + o + 2 + slen > dict_len) { out[0] = 0; return; }
    memcpy(out, dict + data_off + o + 2, slen);
    out[slen] = 0;
}

static void decode_next(unsigned long *po, const char *prev, char *out) {
    unsigned long o = *po;
    unsigned char shared, slen;
    if (data_off + o + 2 > dict_len) { out[0] = 0; return; }
    shared = dict[data_off + o];
    slen   = dict[data_off + o + 1];
    if (shared >= MAX_WORD) shared = MAX_WORD - 1;
    if (slen >= MAX_WORD - shared) slen = MAX_WORD - shared - 1;
    if (data_off + o + 2 + slen > dict_len) { out[0] = 0; return; }
    memcpy(out, prev, shared);
    memcpy(out + shared, dict + data_off + o + 2, slen);
    out[shared + slen] = 0;
    *po = o + 2 + slen;
}

static int dict_lookup(const char *key) {
    long lo = 0, hi = (long)restart_count - 1, mid;
    char buf[MAX_WORD], prev[MAX_WORD];
    unsigned long o;
    int i;
    if (restart_count == 0) return 0;
    while (lo < hi) {
        mid = (lo + hi + 1) / 2;
        decode_restart((unsigned long)mid, buf);
        if (strcmp(buf, key) <= 0) lo = mid; else hi = mid - 1;
    }
    o = rd32(dict + restart_off + 4*lo);
    prev[0] = 0;
    for (i = 0; i < RESTART; i++) {
        decode_next(&o, prev, buf);
        if (buf[0] == 0) break;
        if (strcmp(buf, key) == 0) return 1;
        if (strcmp(buf, key) > 0)  return 0;
        strcpy(prev, buf);
    }
    return 0;
}

/* ---------- Damerau-Levenshtein (no malloc) ---------- */
static int edit_distance(const char *a, const char *b, int cap) {
    int prev[MAX_WORD+1], cur[MAX_WORD+1], prev2[MAX_WORD+1];
    int la = (int)strlen(a), lb = (int)strlen(b);
    int i, j, cost, row_min, result;
    int *p, *c, *p2, *t;
    if (la > MAX_WORD || lb > MAX_WORD) return cap + 1;
    if (abs(la - lb) > cap) return cap + 1;
    for (j = 0; j <= lb; j++) prev[j] = j;
    p2 = prev2; p = prev; c = cur;
    for (i = 1; i <= la; i++) {
        c[0] = i; row_min = i;
        for (j = 1; j <= lb; j++) {
            cost = (a[i-1] == b[j-1]) ? 0 : 1;
            c[j] = p[j] + 1;
            if (c[j-1] + 1 < c[j]) c[j] = c[j-1] + 1;
            if (p[j-1] + cost < c[j]) c[j] = p[j-1] + cost;
            if (i > 1 && j > 1 && a[i-1] == b[j-2] && a[i-2] == b[j-1]) {
                if (p2[j-2] + 1 < c[j]) c[j] = p2[j-2] + 1;
            }
            if (c[j] < row_min) row_min = c[j];
        }
        if (row_min > cap) return cap + 1;
        t = p2; p2 = p; p = c; c = t;
    }
    result = p[lb];
    return result;
}

/* ---------- guesses ---------- */
static int common_prefix(const char *a, const char *b) {
    int n = 0;
    while (a[n] && b[n] && a[n] == b[n]) n++;
    return n;
}

typedef struct { int dist; char word[MAX_WORD]; } Cand;

static const char *sort_key;

static int cmp_cand(const void *x, const void *y) {
    const Cand *a = (const Cand *)x, *b = (const Cand *)y;
    int pa, pb;
    if (a->dist != b->dist) return a->dist - b->dist;
    pa = common_prefix(sort_key, a->word);
    pb = common_prefix(sort_key, b->word);
    if (pa != pb) return pb - pa;
    return strcmp(a->word, b->word);
}

static int make_guesses(const char *key, char out[][MAX_WORD], int max) {
    unsigned long r, o;
    char buf[MAX_WORD], prev[MAX_WORD];
    int i, n = 0, k;
    int klen = (int)strlen(key);
    int lo = klen - 2, hi = klen + 2;
    Cand *c;
    if (lo < 1) lo = 1;
    if (klen == 0) return 0;
    c = (Cand *)malloc(sizeof(Cand) * SCRATCH_CAP);
    if (!c) return 0;
    for (r = 0; r < restart_count && n < SCRATCH_CAP; r++) {
        if (restart_off + 4*r + 4 > dict_len) break;
        o = rd32(dict + restart_off + 4*r);
        prev[0] = 0;
        for (i = 0; i < RESTART; i++) {
            if (data_off + o + 2 > dict_len) break;
            decode_next(&o, prev, buf);
            if (buf[0] == 0) break;
            strcpy(prev, buf);
            if (buf[0] != key[0]) continue;
            if ((int)strlen(buf) < lo || (int)strlen(buf) > hi) continue;
            k = edit_distance(key, buf, EDIT_CAP);
            if (k <= EDIT_CAP && n < SCRATCH_CAP) {
                c[n].dist = k;
                strncpy(c[n].word, buf, MAX_WORD - 1);
                c[n].word[MAX_WORD - 1] = 0;
                n++;
            }
        }
    }
    sort_key = key;
    qsort(c, n, sizeof(Cand), cmp_cand);
    for (i = 0; i < n && i < max; i++) strcpy(out[i], c[i].word);
    free(c);
    return (n < max) ? n : max;
}

/* ---------- case ---------- */
static void match_case(const char *tmpl, const char *word, char *out) {
    int i, upper = 1, lower = 1;
    for (i = 0; tmpl[i]; i++) {
        if (isupper((unsigned char)tmpl[i])) lower = 0;
        if (islower((unsigned char)tmpl[i])) upper = 0;
    }
    if (upper) { for (i = 0; word[i]; i++) out[i] = toupper((unsigned char)word[i]); out[i] = 0; }
    else if (lower) { for (i = 0; word[i]; i++) out[i] = tolower((unsigned char)word[i]); out[i] = 0; }
    else if (isupper((unsigned char)tmpl[0])) {
        out[0] = toupper((unsigned char)word[0]);
        for (i = 1; word[i]; i++) out[i] = tolower((unsigned char)word[i]);
        out[i] = 0;
    } else { for (i = 0; word[i]; i++) out[i] = tolower((unsigned char)word[i]); out[i] = 0; }
}

/* ---------- text helpers ---------- */
static int next_word(const char *text, int len, int *pos, int *start) {
    int i = *pos;
    while (i < len && !isalpha((unsigned char)text[i])) i++;
    *start = i;
    while (i < len && (isalpha((unsigned char)text[i]) || text[i] == '\'')) i++;
    *pos = i;
    return i - *start;
}

static void sentence_bounds(const char *text, int len, int start, int end,
                            int *sbeg, int *send, int *ws, int *we) {
    int i;
    *sbeg = 0;
    for (i = start - 1; i >= 0; i--) {
        if (text[i] == '.' || text[i] == '!' || text[i] == '?' || text[i] == '\n') { *sbeg = i + 1; break; }
    }
    *send = len;
    for (i = end; i < len; i++) {
        if (text[i] == '.' || text[i] == '!' || text[i] == '?' || text[i] == '\n') { *send = i + 1; break; }
    }
    while (*sbeg < *send && isspace((unsigned char)text[*sbeg])) (*sbeg)++;
    *ws = start - *sbeg;
    *we = end - *sbeg;
}

/* ---------- I/O ---------- */
#ifdef QDOS
static int getch_(void) {
    char c;
    fflush(stdout);
    if (io_fbyte(1, 0, &c) != 0) return -1;
    return (unsigned char)c;
}
#else
static int getch_(void) {
    int c;
    fflush(stdout);
    c = getchar();
    return (c == EOF) ? -1 : c;
}
#endif

static void print_(const char *s) { fputs(s, stdout); }
static void nln_(void) { fputc('\n', stdout); }

static int read_line_(char *buf, int n) {
    int i = 0, c;
    fflush(stdout);
    while ((c = getch_()) != -1 && c != '\n' && c != '\r') {
        if (c == 8 || c == 127) { if (i > 0) i--; continue; }
        if (i < n - 1) buf[i++] = (char)c;
    }
    buf[i] = 0;
    return i;
}

/* ---------- colour ---------- */
#ifdef QDOS
static void set_red(void) { }
static void set_normal(void) { }
#else
#ifdef _WIN32
static void set_red(void) {
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    SetConsoleTextAttribute(h, FOREGROUND_RED | FOREGROUND_INTENSITY);
}
static void set_normal(void) {
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    SetConsoleTextAttribute(h, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
}
#else
static void set_red(void) { printf("\x1b[31m"); }
static void set_normal(void) { printf("\x1b[0m"); }
#endif
#endif

static void print_sentence(const char *text, int sbeg, int wstart, int wlen, int send) {
    int k;
#ifdef QDOS
    for (k = sbeg; k < wstart; k++) fputc(text[k], stdout);
    fputc('[', stdout);
    for (k = wstart; k < wstart + wlen; k++) fputc(text[k], stdout);
    fputc(']', stdout);
    for (k = wstart + wlen; k < send; k++) fputc(text[k], stdout);
#else
    for (k = sbeg; k < wstart; k++) fputc(text[k], stdout);
    set_red();
    fputc('[', stdout);
    for (k = wstart; k < wstart + wlen; k++) fputc(text[k], stdout);
    fputc(']', stdout);
    set_normal();
    for (k = wstart + wlen; k < send; k++) fputc(text[k], stdout);
#endif
}

/* ---------- menu ---------- */
static int prompt_choice(const char *word,
                         const char *text, int sbeg, int wstart, int wlen, int send,
                         int use_colour,
                         char gs[][MAX_WORD], int ng) {
    int page = 0;
    for (;;) {
        int start = page * PAGE_SIZE, end = start + PAGE_SIZE;
        int i, c;
        char line[16];
        if (end > ng) end = ng;
        nln_();
        if (use_colour) {
            fputs("  ", stdout);
            print_sentence(text, sbeg, wstart, wlen, send);
        } else {
            fputs(text, stdout);
        }
        nln_();
        for (i = start; i < end; i++) {
            char m[MAX_WORD];
            match_case(word, gs[i], m);
            printf("   %d. %s\n", i - start + 1, m);
        }
        if (end < ng) printf("   6. more guesses (%d left)\n", ng - end);
        print_("   7. ignore\n");
        print_("   8. replace\n");
        print_("   q. quit and save\n");
        print_("choose: ");
        fflush(stdout);
        if (read_line_(line, sizeof line) == 0) return 0;
        c = (unsigned char)line[0];
        if (c == 'q' || c == 'Q') return -1;
        if (c == 27) return 0;
        if (c >= '1' && c <= '5') {
            int n = c - '1';
            if (start + n < end) return start + n + 1;
        }
        if (c == '6') { if (end < ng) page++; continue; }
        if (c == '7') return -2;
        if (c == '8') return -3;
    }
}

static int ask_replacement(const char *original, char out[MAX_WORD]) {
    char line[MAX_WORD];
    char key[MAX_WORD];
    for (;;) {
        int i;
        printf("replace [%s] with (q=quit, empty=cancel): ", original);
        fflush(stdout);
        if (read_line_(line, MAX_WORD) == 0) return 0;
        if (line[0] == 'q' || line[0] == 'Q') return -1;
        {
            int ok = 1;
            for (i = 0; line[i]; i++) if (!isalpha((unsigned char)line[i])) { ok = 0; break; }
            if (!ok) { print_("  letters only\n"); continue; }
        }
        for (i = 0; line[i]; i++) key[i] = toupper((unsigned char)line[i]);
        key[i] = 0;
        if (dict_lookup(key)) { strcpy(out, line); return 1; }
        printf("  '%s' is not in the dictionary.\n", line);
        {
            static char gs[MAX_GUESSES][MAX_WORD];
            char hdr[MAX_WORD + 32];
            int ng;
            int r;
            sprintf(hdr, "  replace [%s] with:", line);
            ng = make_guesses(key, gs, MAX_GUESSES);
            r = prompt_choice(line, hdr, 0, 0, 0, (int)strlen(hdr), 0, gs, ng);
            if (r == -1) return -1;
            if (r == -2) return 0;
            if (r > 0) { match_case(line, gs[r-1], out); return 1; }
        }
    }
}

/* ---------- main ---------- */
static int save_file(const char *path, const char *text, int len) {
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    fwrite(text, 1, len, f);
    fclose(f);
    return 1;
}

int main(int argc, char **argv) {
    const char *dictpath, *inpath, *outpath;
    char *text, *tmp;
    long n;
    int len, pos = 0, fixes = 0, quit = 0;
    FILE *f;

    setvbuf(stdout, NULL, _IONBF, 0);

    if (argc != 4) {
        print_("usage: spell dict.smd input.txt output.txt\n");
        return 1;
    }
    dictpath = argv[1]; inpath = argv[2]; outpath = argv[3];
    print_("loading "); print_(dictpath); print_("...\n");
    if (!load_dict(dictpath)) { print_("bad dictionary\n"); return 1; }
    printf("  %lu words loaded\n", word_count);

    f = fopen(inpath, "rb");
    if (!f) { print_("cannot open input\n"); return 1; }
    fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
    text = (char *)malloc((size_t)n + 1);
    if (!text) { fclose(f); return 1; }
    if (fread(text, 1, (size_t)n, f) != (size_t)n) { fclose(f); return 1; }
    fclose(f); len = (int)n; text[len] = 0;

    tmp = (char *)malloc(strlen(outpath) + 5);
    if (!tmp) return 1;
    sprintf(tmp, "%s.tmp", outpath);
    save_file(tmp, text, len);

    while (pos < len) {
        int wstart, wlen, sbeg, send, ws, we;
        char word[MAX_WORD], key[MAX_WORD];
        int i;

        wlen = next_word(text, len, &pos, &wstart);
        if (wlen == 0) break;
        if (wlen >= MAX_WORD) continue;
        memcpy(word, text + wstart, wlen); word[wlen] = 0;
        for (i = 0; i < wlen; i++) key[i] = toupper((unsigned char)word[i]);
        key[wlen] = 0;
        if (dict_lookup(key)) continue;

        sentence_bounds(text, len, wstart, wstart + wlen, &sbeg, &send, &ws, &we);

        {
            static char gs[MAX_GUESSES][MAX_WORD];
            int ng = make_guesses(key, gs, MAX_GUESSES);
            int r = prompt_choice(word, text, sbeg, wstart, wlen, send, 1, gs, ng);
            if (r == -1) { quit = 1; break; }
            if (r == -2 || r == 0) continue;
            if (r == -3) {
                char rep[MAX_WORD];
                int kk = ask_replacement(word, rep);
                if (kk == -1) { quit = 1; break; }
                if (kk == 0) continue;
                {
                    int rl = (int)strlen(rep);
                    char *nt = (char *)malloc((size_t)len + rl + 1);
                    if (!nt) break;
                    memcpy(nt, text, wstart);
                    memcpy(nt + wstart, rep, rl);
                    memcpy(nt + wstart + rl, text + wstart + wlen, len - wstart - wlen + 1);
                    free(text); text = nt;
                    len = len - wlen + rl;
                    pos = wstart + rl;
                    save_file(tmp, text, len);
                    fixes++;
                }
                continue;
            }
            {
                char rep[MAX_WORD];
                int rl;
                match_case(word, gs[r-1], rep);
                rl = (int)strlen(rep);
                {
                    char *nt = (char *)malloc((size_t)len + rl + 1);
                    if (!nt) break;
                    memcpy(nt, text, wstart);
                    memcpy(nt + wstart, rep, rl);
                    memcpy(nt + wstart + rl, text + wstart + wlen, len - wstart - wlen + 1);
                    free(text); text = nt;
                    len = len - wlen + rl;
                    pos = wstart + rl;
                    save_file(tmp, text, len);
                    fixes++;
                }
            }
        }
    }
    save_file(outpath, text, len);
    remove(tmp);
    printf("%s (%d replacements%s)\n", outpath, fixes, quit ? " before quit" : "");
    free(tmp); free(text); free(dict);
    return 0;
}