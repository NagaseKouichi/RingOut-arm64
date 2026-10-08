/* native_skin.c -- --native-skin: native fast paths for SoulCalibur II's CPU skinning loops.
 *
 * The game skins character meshes on the CPU in one guest function (US/Plus 0x801269D4, JP 0x8012396C,
 * PAL 0x8012B494): loops over one-, two- and three-bone vertices that load a 3x4 matrix from a palette,
 * dequantise a s16 position and normal through GQR6/GQR7, transform both and store floats. It is ~7% of a
 * fight's CPU. Measured on the Steam Deck (US, both arms PGO-retrained, n=6): -1.91% cycles, -5.18%
 * instructions, frame hash identical.
 *
 * This is a POST-PASS over the emitted chunk C, not a new emitter: each loop's emitted statements are copied
 * with guest registers renamed to locals and the per-instruction bookkeeping removed (MSR.FP re-tests,
 * exception re-checks, pc stores); psq/lha/lwz become direct RAM accesses, each iteration guarded first by
 * checks that make every access EXACTLY the inlined fast path cpu.h would take (nat_skin.h, embedded below).
 * Arithmetic statements are copied verbatim, so rounding cannot drift. A failed guard writes the state back
 * and falls into the original emitted code for that iteration. Cycle charging, the back-edge's
 * DOLRECOMP_DC <= -1000 exit, the CR0 variable and lazy FPRF are reproduced exactly. Any statement the pass
 * does not recognise leaves that loop untouched, with a warning -- never a guess.
 *
 * Output is byte-identical to work/skin-native/transform.py (the prototype measured above) for the hot copy;
 * this version also patches the cold twin copy, taking the CR0 expression from the code it rewrites.
 *
 * The function is found by its instruction signature (the top 16 bits of all 441 words: opcode and
 * register fields, displacements excluded -- JP and PAL differ only in 8 displacements), so no per-disc
 * address table is needed. */
#include "backend/native_skin.h"
#include "platform/fs.h"

#include <stdbool.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool s_enabled = false;
void native_skin_enable(bool on) { s_enabled = on; }

static const unsigned short kSig[] = {
#include "native_skin_sig.inc"
};
#define SIG_WORDS (sizeof(kSig) / sizeof(kSig[0]))

static const char kHeader[] =
#include "native_skin_header.inc"
    ;

/* loops: body start, test (or CTR back-edge) start, exit -- offsets from the function's first
 * instruction. Loops 1-3 close on `bc 12,0` (CR0.LT from a cmplw); loop 4's inner per-bone loop closes
 * on `bc 16,0` (decrement CTR, branch while non-zero). */
enum { LOOP_CR = 0, LOOP_CTR = 1 };
static const struct { u32 body, test, exit; int kind; } kLoops[4] = {
    {0xD8, 0x174, 0x180, LOOP_CR}, {0x188, 0x2B0, 0x2BC, LOOP_CR}, {0x2C4, 0x478, 0x484, LOOP_CR},
    {0x528, 0x5D8, 0x5DC, LOOP_CTR}};
#define NUM_LOOPS (int)(sizeof(kLoops) / sizeof(kLoops[0]))

static u32 be32(const u8* p) { return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3]; }

static u32 find_function(const LoadedCodeSection* sections, u32 count, u32* hits) {
    u32 found = 0, at = 0;
    for (u32 s = 0; s < count; s++) {
        const LoadedCodeSection* sec = &sections[s];
        for (u32 k = 0; k + SIG_WORDS * 4u <= sec->size; k += 4u) {
            if (be32(sec->data + k) != 0x9421FED0u) /* stwu r1,-304(r1) */
                continue;
            u32 i = 0;
            while (i < SIG_WORDS && (be32(sec->data + k + 4u * i) >> 16) == kSig[i])
                i++;
            if (i == SIG_WORDS) {
                found++;
                at = sec->address + k;
            }
        }
    }
    *hits = found;
    return at;
}

/* ---- a growable line list ---- */
typedef struct { char** v; size_t n, cap; } Lines;
static void lines_push(Lines* L, char* s) {
    if (L->n == L->cap) {
        L->cap = L->cap ? L->cap * 2 : 1024;
        L->v = realloc(L->v, L->cap * sizeof(char*));
    }
    L->v[L->n++] = s;
}
static void lines_free(Lines* L) { for (size_t i = 0; i < L->n; i++) free(L->v[i]); free(L->v); }

/* ---- a growable string ---- */
typedef struct { char* p; size_t n, cap; } Str;
static void str_add(Str* s, const char* t, size_t k) {
    if (s->n + k + 1 > s->cap) {
        s->cap = (s->n + k + 1) * 2;
        s->p = realloc(s->p, s->cap);
    }
    memcpy(s->p + s->n, t, k);
    s->n += k;
    s->p[s->n] = 0;
}
static void str_puts(Str* s, const char* t) { str_add(s, t, strlen(t)); }
#include <stdarg.h>
static void str_printf(Str* s, const char* fmt, ...) {
    char buf[4096];
    va_list ap;
    va_start(ap, fmt);
    int k = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (k > 0) str_add(s, buf, (size_t)k);
}

static const char* skip_ws(const char* s) { while (*s == ' ' || *s == '\t') s++; return s; }
static bool starts(const char* s, const char* p) { return strncmp(s, p, strlen(p)) == 0; }

/* Is this stripped line `label_XXXXXXXX:` / `fpok_XXXXXXXX:`? */
static bool is_hexlabel(const char* s, const char* prefix) {
    size_t k = strlen(prefix);
    if (strncmp(s, prefix, k) != 0) return false;
    for (int i = 0; i < 8; i++) {
        char c = s[k + i];
        if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F'))) return false;
    }
    return s[k + 8] == ':' && s[k + 9] == 0;
}

typedef struct { bool g[32], f[32]; } Regs;

/* Replace every `ctx->gpr[N]` / `ctx->fpr[N]` / `ctx->ps1[N]` with nGN / nFN / nPN, recording N. */
static char* rename_regs(const char* l, Regs* r) {
    Str o = {0};
    const char* p = l;
    while (*p) {
        const char* kinds[3] = {"ctx->gpr[", "ctx->fpr[", "ctx->ps1["};
        const char* names[3] = {"nG", "nF", "nP"};
        int which = -1;
        for (int k = 0; k < 3; k++)
            if (starts(p, kinds[k])) { which = k; break; }
        if (which >= 0) {
            const char* q = p + 9;
            char* end;
            long n = strtol(q, &end, 10);
            if (end != q && *end == ']' && n >= 0 && n < 32) {
                if (which == 0) r->g[n] = true; else r->f[n] = true;
                str_printf(&o, "%s%ld", names[which], n);
                p = end + 1;
                continue;
            }
        }
        str_add(&o, p, 1);
        p++;
    }
    if (!o.p) str_puts(&o, "");
    return o.p;
}

static char* replace_all(const char* l, const char* from, const char* to) {
    Str o = {0};
    const char* p = l;
    size_t k = strlen(from);
    while (*p) {
        if (strncmp(p, from, k) == 0) { str_puts(&o, to); p += k; }
        else { str_add(&o, p, 1); p++; }
    }
    if (!o.p) str_puts(&o, "");
    return o.p;
}

/* Does the line still touch ctx-> other than ctx->crf[N] / ctx->xer? */
static bool has_bad_ctx(const char* l) {
    for (const char* p = strstr(l, "ctx->"); p; p = strstr(p + 5, "ctx->"))
        if (!starts(p, "ctx->crf[") && !starts(p, "ctx->xer")) return true;
    return false;
}

/* Transform lines [a,b) of src into out. psq lines carry their own 8-space indent (as in transform.py);
 * gen_loop indents every line by 8 more. false = unsupported shape. */
static bool transform(char** src, size_t a, size_t b, Lines* out, Regs* r) {
    for (size_t i = a; i < b; i++) {
        const char* l = src[i];
        const char* s = skip_ws(l);
        if (is_hexlabel(s, "label_") || is_hexlabel(s, "fpok_") || starts(s, "goto fpok_") ||
            starts(s, "ctx->pc =") || strstr(s, "ppc_fp_available") || starts(s, "if (ctx->exception)") ||
            starts(s, "PPC_FPRF_TAG") || starts(s, "PPC_CR_WRITE") || starts(s, "PPC_CR_READ") || *s == 0)
            continue;
        unsigned n, q, cia;
        char wflag[8];
        if (sscanf(s, "ppc_psq_load(ctx, %uu, ea, %5[a-z], %uu, false, 0x%Xu);", &n, wflag, &q, &cia) == 4) {
            if (strcmp(wflag, "false") != 0 || n >= 32) return false;
            char buf[96];
            snprintf(buf, sizeof(buf), "        NAT_PSQ_L(%u, ea, %u);", n, q);
            if (q != 0 && q != 6 && q != 7) return false;
            lines_push(out, strdup(buf));
            r->f[n] = true;
            continue;
        }
        if (sscanf(s, "ppc_psq_store(ctx, %uu, ea, %5[a-z], %uu, false, 0x%Xu);", &n, wflag, &q, &cia) == 4) {
            if (strcmp(wflag, "false") != 0 || q != 0 || n >= 32) return false;
            char buf[96];
            snprintf(buf, sizeof(buf), "        NAT_PSQ_ST(%u, ea);", n);
            lines_push(out, strdup(buf));
            r->f[n] = true;
            continue;
        }
        char* t = strdup(l);
        char* u;
        if (strstr(t, "dolrecomp_fprf_s(")) { u = replace_all(t, "dolrecomp_fprf_s(ctx, ", "nat_fprf = ("); free(t); t = u; }
        if (strstr(t, "mem_read16(ctx, ea)")) { u = replace_all(t, "mem_read16(ctx, ea)", "NAT_R16(ea)"); free(t); t = u; }
        if (strstr(t, "mem_read32(ctx, ea)")) { u = replace_all(t, "mem_read32(ctx, ea)", "NAT_R32(ea)"); free(t); t = u; }
        char* nonat = replace_all(t, "NAT_", "");
        bool helper = strstr(t, "mem_read") || strstr(t, "mem_write") || strstr(nonat, "ppc_");
        free(nonat);
        if (helper) { free(t); return false; }
        u = rename_regs(t, r);
        free(t);
        if (has_bad_ctx(u)) { free(u); return false; }
        lines_push(out, u); /* gen_loop adds the 8-space indent, as transform.py does */
    }
    return true;
}

static bool find_label_from(char** v, size_t n, size_t from, u32 pc, size_t* at) {
    char want[32];
    snprintf(want, sizeof(want), "label_%08X:", pc);
    for (size_t i = from; i < n; i++)
        if (strcmp(skip_ws(v[i]), want) == 0 && v[i][0] == 'l') { *at = i; return true; }
    return false;
}

/* Build the native block for one loop copy whose body label is at line `bi`. NULL = shape not supported. */
static char* gen_loop(char** v, size_t n, size_t bi, u32 body, u32 test, u32 exit, int tag, int kind) {
    size_t ti, ei;
    if (!find_label_from(v, n, bi, test, &ti) || !find_label_from(v, n, ti, exit, &ei)) return NULL;
    /* the back-edge. CR kind: `bc 12, 0, <body>` on CR0.LT, CR0 expression taken from the code itself.
     * CTR kind: `bc 16, 0, <body>`, emitted as ctx->ctr--; ctr_ok = ctr != 0; cr_ok = true. */
    size_t bc = 0;
    char cr[32] = "";
    if (kind == LOOP_CTR) {
        bool dec = false, ok = false, crt = false;
        for (size_t i = ti; i < ei; i++) {
            const char* t = skip_ws(v[i]);
            if (strstr(t, "bc    16, 0")) bc = i;
            if (strcmp(t, "ctx->ctr--;") == 0) dec = true;
            if (strcmp(t, "bool ctr_ok = (((ctx->ctr != 0) ? 1u : 0u) ^ 0u) != 0;") == 0) ok = true;
            if (strcmp(t, "bool cr_ok = true;") == 0) crt = true;
        }
        if (!bc || !dec || !ok || !crt) return NULL;
    } else {
        for (size_t i = ti; i < ei; i++)
            if (strstr(v[i], "bc    12, 0")) { bc = i; break; }
        if (!bc) return NULL;
        for (size_t i = bc; i < ei; i++) {
            const char* p = strstr(v[i], "bool cr_ok = (((");
            if (p) {
                p += strlen("bool cr_ok = (((");
                const char* e = strstr(p, " & 0x8u) != 0) == true);");
                if (!e || (size_t)(e - p) >= sizeof(cr)) return NULL;
                memcpy(cr, p, (size_t)(e - p));
                cr[e - p] = 0;
                break;
            }
        }
        if (strcmp(cr, "dr_c0") != 0 && strcmp(cr, "ctx->crf[0]") != 0) return NULL;

    }
    Regs r = {0};
    Lines bs = {0}, ts = {0};
    if (!transform(v, bi, ti, &bs, &r) || (kind == LOOP_CR && !transform(v, ti, bc, &ts, &r))) { lines_free(&bs); lines_free(&ts); return NULL; }

    /* registers r0, r3..r6 the guard needs: a twin-copy local (dr_gN) when the copy has one */
    bool dr[32] = {0};
    for (size_t i = bi; i < ei; i++)
        for (int k = 0; k < 32; k++) {
            char nm[8];
            snprintf(nm, sizeof(nm), "dr_g%d", k);
            const char* p = strstr(v[i], nm);
            while (p) {
                char c = p[strlen(nm)];
                if (!(c >= '0' && c <= '9')) { dr[k] = true; break; }
                p = strstr(p + 1, nm);
            }
        }
    const int guard_regs[5] = {0, 3, 4, 5, 6};
    for (int k = 0; k < 5; k++)
        if (!dr[guard_regs[k]]) r.g[guard_regs[k]] = true;

    Str o = {0};
    str_printf(&o, "    /* NATIVE SKIN LOOP L%d (transform.py): guest %08X..%08X */\n", tag, body, exit - 4);
    str_puts(&o, "    if (nat_skin_ok(ctx)) {\n        ");
    bool first = true;
    for (int k = 0; k < 32; k++)
        if (r.g[k]) { str_printf(&o, "%su32 nG%d = ctx->gpr[%d];", first ? "" : " ", k, k); first = false; }
    if (kind == LOOP_CTR) str_puts(&o, " u32 nCTR = ctx->ctr;");
    str_puts(&o, "\n        ");
    first = true;
    for (int k = 0; k < 32; k++)
        if (r.f[k]) { str_printf(&o, "%sf64 nF%d = ctx->fpr[%d], nP%d = ctx->ps1[%d];", first ? "" : " ", k, k, k, k); first = false; }
    str_puts(&o, "\n        f32 nat_fprf = 0.0f; bool nat_any = false;\n#define NAT_WB() do { ");
    first = true;
    for (int k = 0; k < 32; k++)
        if (r.g[k]) { str_printf(&o, "%sctx->gpr[%d] = nG%d;", first ? "" : " ", k, k); first = false; }
    str_puts(&o, " ");
    first = true;
    for (int k = 0; k < 32; k++)
        if (r.f[k]) { str_printf(&o, "%sctx->fpr[%d] = nF%d; ctx->ps1[%d] = nP%d;", first ? "" : " ", k, k, k, k); first = false; }
    if (kind == LOOP_CTR) str_puts(&o, " ctx->ctr = nCTR;");
    str_puts(&o, " if (nat_any) dolrecomp_fprf_s(ctx, nat_fprf); } while (0)\n        for (;;) {\n");
    str_printf(&o, "            if (!nat_iter_ok_L%d(NAT_RAM", tag);
    for (int k = 0; k < 5; k++) {
        int g = guard_regs[k];
        str_printf(&o, ", %s%d", dr[g] ? "dr_g" : "nG", g);
    }
    str_printf(&o, ")) { NAT_WB(); goto nat_fall_%08X; }\n", body);
    for (size_t i = 0; i < bs.n; i++) { str_puts(&o, "        "); str_puts(&o, bs.v[i]); str_puts(&o, "\n"); }
    for (size_t i = 0; i < ts.n; i++) { str_puts(&o, "        "); str_puts(&o, ts.v[i]); str_puts(&o, "\n"); }
    str_puts(&o, "            nat_any = true;\n");
    if (kind == LOOP_CTR)
        str_printf(&o, "            if (--nCTR == 0u) { NAT_WB(); goto label_%08X; }\n", exit);
    else
        str_printf(&o, "            if (!(%s & 0x8u)) { NAT_WB(); goto label_%08X; }\n", cr, exit);
    str_printf(&o, "            if (DOLRECOMP_DC <= -1000) { NAT_WB(); ctx->pc = 0x%08Xu; DOLRECOMP_RETURN; }\n", body);
    str_printf(&o, "        }\n#undef NAT_WB\n    }\nnat_fall_%08X:", body);
    lines_free(&bs);
    lines_free(&ts);
    return o.p;
}

static bool patch_file(const char* path, u32 fn, unsigned* patched) {
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* text = malloc((size_t)size + 1);
    if (fread(text, 1, (size_t)size, f) != (size_t)size) { fclose(f); free(text); return false; }
    fclose(f);
    text[size] = 0;
    /* dolrecomp.exe writes the chunks with CRLF: drop every \r, or no line compare below ever matches on
     * Windows (the 1.6.7 Windows build made 0 skin loops native). The file is written back with LF. */
    {
        char* w = text;
        for (const char* r = text; *r; r++)
            if (*r != '\r') *w++ = *r;
        *w = 0;
    }
    char want[32];
    snprintf(want, sizeof(want), "\nlabel_%08X:", fn + kLoops[0].body);
    if (!strstr(text, want)) { free(text); return true; } /* not this chunk */

    /* split into lines (keeping the final empty element, as Python's split does) */
    Lines L = {0};
    for (char *p = text, *q;; p = q + 1) {
        q = strchr(p, '\n');
        if (!q) { lines_push(&L, strdup(p)); break; }
        *q = 0;
        lines_push(&L, strdup(p));
    }
    free(text);

    /* every copy of each loop: insert the native block after its body label */
    unsigned count = 0;
    bool any_fail = false;
    for (int lp = 0; lp < NUM_LOOPS; lp++) {
        size_t from = 0, at;
        while (find_label_from(L.v, L.n, from, fn + kLoops[lp].body, &at)) {
            char* code = gen_loop(L.v, L.n, at, fn + kLoops[lp].body, fn + kLoops[lp].test, fn + kLoops[lp].exit, lp + 1, kLoops[lp].kind);
            if (!code) {
                fprintf(stderr, "native-skin: loop L%d at %08X in %s has an unexpected shape; left as emitted\n",
                        lp + 1, fn + kLoops[lp].body, path);
                any_fail = true;
                from = at + 1;
                continue;
            }
            lines_push(&L, NULL); /* grow by one, then shift */
            memmove(&L.v[at + 2], &L.v[at + 1], (L.n - at - 2) * sizeof(char*));
            L.v[at + 1] = code;
            count++;
            from = at + 2;
        }
    }
    if (count == 0) { lines_free(&L); return !any_fail; }

    /* the helpers, once, after the generated.h include */
    bool header_done = false;
    FILE* w = fopen(path, "wb");
    if (!w) { lines_free(&L); return false; }
    for (size_t i = 0; i < L.n; i++) {
        fputs(L.v[i], w);
        if (!header_done && strcmp(L.v[i], "#include \"../generated.h\"") == 0) {
            fputs("\n", w);
            fputs(kHeader, w);
            header_done = true;
            continue; /* kHeader ends in a newline: the next line follows directly */
        }
        if (i + 1 < L.n) fputs("\n", w);
    }
    fclose(w);
    lines_free(&L);
    if (!header_done) {
        fprintf(stderr, "native-skin: %s has no generated.h include; helpers missing\n", path);
        return false;
    }
    *patched += count;
    return !any_fail;
}

bool native_skin_apply(const LoadedCodeSection* sections, u32 count, const char* chunks_dir) {
    if (!s_enabled) return true;
    u32 hits = 0;
    u32 fn = find_function(sections, count, &hits);
    if (hits != 1) {
        printf("native-skin: skinning function %s; nothing patched\n", hits ? "found more than once" : "not found");
        return true;
    }
    /* every .c in the chunks directory: FindFirstFile on Windows (dolrecomp.exe is built with MSVC), dirent
     * elsewhere -- the same split platform/pathlist.c uses */
    unsigned patched = 0;
    bool ok = true;
#ifdef _WIN32
    char pattern[2048];
    if (!join_path(pattern, sizeof(pattern), chunks_dir, "*.c")) return false;
    WIN32_FIND_DATAA data;
    HANDLE find = FindFirstFileA(pattern, &data);
    if (find == INVALID_HANDLE_VALUE) { fprintf(stderr, "native-skin: cannot list %s\n", chunks_dir); return false; }
    do {
        const char* name = data.cFileName;
#else
    DIR* d = opendir(chunks_dir);
    if (!d) { fprintf(stderr, "native-skin: cannot list %s\n", chunks_dir); return false; }
    for (struct dirent* e; (e = readdir(d));) {
        const char* name = e->d_name;
#endif
        size_t k = strlen(name);
        if (k >= 3 && strcmp(name + k - 2, ".c") == 0) {
            char path[2048];
            if (!join_path(path, sizeof(path), chunks_dir, name) || !patch_file(path, fn, &patched))
                ok = false;
        }
#ifdef _WIN32
    } while (FindNextFileA(find, &data));
    FindClose(find);
#else
    }
    closedir(d);
#endif
    printf("native-skin: skinning function at %08X, %u loop copies made native\n", fn, patched);
    return ok;
}
