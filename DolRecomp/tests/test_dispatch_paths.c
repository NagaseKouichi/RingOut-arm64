/* The dispatch-reduction flags (all off by default): --loop-budget, --dynamic-loop-budget, --jump-tables,
 * --indirect-calls and --function-overhang. Each test pins the text the emitter writes for a small synthetic program; the 16000-frame
 * hash, the dispatch counts and the Deck A/B are the end-to-end checks. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/common/types.h"
#include "../src/cpu/cpu.h"
#include "../src/frontend/decoder.h"
#include "../src/backend/emitter.h"
#include "../src/backend/dispatch.h"

#define BASE 0x80000000u
#define TABLE_ADDR 0x80002000u

static int failures = 0;

static void expect(const char* test, int cond, const char* what) {
    if (!cond) {
        fprintf(stderr, "FAIL %s: %s\n", test, what);
        failures++;
    }
}

static u32 dform(u32 op, u32 rt, u32 ra, u16 imm) { return (op << 26) | (rt << 21) | (ra << 16) | imm; }
static u32 rlwinm(u32 rs, u32 ra, u32 sh, u32 mb, u32 me) {
    return (21u << 26) | (rs << 21) | (ra << 16) | (sh << 11) | (mb << 6) | (me << 1);
}
static u32 lwzx(u32 rt, u32 ra, u32 rb) { return (31u << 26) | (rt << 21) | (ra << 16) | (rb << 11) | (23u << 1); }
static u32 mtctr(u32 rs) { return (31u << 26) | (rs << 21) | (9u << 16) | (467u << 1); }
static u32 branch(u32 from, u32 to) { return (18u << 26) | ((to - from) & 0x03FFFFFCu); }
#define BCTR   0x4E800420u
#define BCTRL  0x4E800421u
#define BLR    0x4E800020u
#define NOP    0x60000000u

/* Emit `raw` as one chunk at BASE (own == count: no overhang) and return the text. */
static char* emit_text(const u32* raw, u32 n) {
    PPCInst* insts = calloc(n, sizeof(PPCInst));
    for (u32 i = 0; i < n; i++)
        insts[i] = ppc_decode(raw[i], BASE + i * 4u);
    FILE* f = tmpfile();
    emit_function(f, insts, n, n, BASE);
    fflush(f);
    long size = ftell(f);
    rewind(f);
    char* buf = malloc((size_t)size + 1u);
    size_t got = fread(buf, 1, (size_t)size, f);
    buf[got] = '\0';
    fclose(f);
    free(insts);
    return buf;
}

static char* header_text(void) {
    FILE* f = tmpfile();
    emit_header(f);
    fflush(f);
    long size = ftell(f);
    rewind(f);
    char* buf = malloc((size_t)size + 1u);
    size_t got = fread(buf, 1, (size_t)size, f);
    buf[got] = '\0';
    fclose(f);
    return buf;
}

/* The bctr's own block: from its `// <pc>: bctr` comment to the next instruction's label. */
static char* block_of(const char* text, const char* comment) {
    const char* a = strstr(text, comment);
    if (!a) return NULL;
    const char* b = strstr(a, "\nlabel_");
    size_t n = b ? (size_t)(b - a) : strlen(a);
    char* out = malloc(n + 1u);
    memcpy(out, a, n);
    out[n] = '\0';
    return out;
}

/* ---- --loop-budget ----------------------------------------------------------------------------------- */
static void test_loop_budget(void) {
    const char* T = "loop_budget";
    /* 0: li r3, 0 ; 4: addi r3, r3, 1 ; 8: b 4 (back-edge) */
    u32 raw[] = {dform(14, 3, 0, 0), dform(14, 3, 3, 1), branch(BASE + 8u, BASE + 4u), BLR};
    char* d = emit_text(raw, 4);
    expect(T, strstr(d, "DOLRECOMP_DC <= -1000)") != NULL, "default budget is 1000");
    emit_set_loop_budget(4000);
    expect(T, emit_loop_budget() == 4000u, "getter returns the set budget");
    char* b = emit_text(raw, 4);
    expect(T, strstr(b, "DOLRECOMP_DC <= -4000)") != NULL && strstr(b, "-1000)") == NULL, "back-edge uses 4000");
    emit_set_loop_budget(0);
    expect(T, emit_loop_budget() == 1000u, "0 restores the default");
    free(d);
    free(b);
}

/* ---- --dynamic-loop-budget --------------------------------------------------------------------------- */
static char* helpers_text(void) {
    FunctionList funcs = {0};
    FILE* f = tmpfile();
    emit_dispatch_helpers(f, &funcs, BASE);
    fflush(f);
    long size = ftell(f);
    rewind(f);
    char* buf = malloc((size_t)size + 1u);
    size_t got = fread(buf, 1, (size_t)size, f);
    buf[got] = '\0';
    fclose(f);
    return buf;
}

static void test_dynamic_loop_budget(void) {
    const char* T = "dynamic_loop_budget";
    u32 raw[] = {dform(14, 3, 0, 0), dform(14, 3, 3, 1), branch(BASE + 8u, BASE + 4u), BLR};
    char* off = helpers_text();
    expect(T, strstr(off, "DOLRECOMP_DYN_LOOP_BUDGET") == NULL, "off by default: no budget exported");
    expect(T, strstr(off, "DOLRECOMP_TIMING_ID 4u") == NULL, "off by default: timing class unchanged");
    emit_set_loop_budget(4000);
    char* before = emit_text(raw, 4);
    emit_set_dynamic_loop_budget(1);
    expect(T, emit_dynamic_loop_budget_enabled(), "getter reports it on");
    char* on = helpers_text();
    expect(T, strstr(on, "#define DOLRECOMP_DYN_LOOP_BUDGET 4000u") != NULL, "exports the budget the chunks use");
    expect(T, strstr(on, "#define DOLRECOMP_TIMING_ID 4u") != NULL, "its own timing class");
    char* after = emit_text(raw, 4);
    expect(T, strcmp(before, after) == 0, "chunk code is unchanged");
    emit_set_dynamic_loop_budget(0);
    emit_set_loop_budget(0);
    free(off);
    free(on);
    free(before);
    free(after);
}

/* ---- --jump-tables ----------------------------------------------------------------------------------- */
enum { JT_N = 16 };
static void jt_program(u32 raw[JT_N]) {
    raw[0] = dform(10, 0, 3, 3);                  /* cmplwi r3, 3                     */
    raw[1] = 0x41810000u | (0x3Cu & 0xFFFCu);     /* bgt 0x80000040 (default)         */
    raw[2] = dform(15, 11, 0, 0x8000);            /* lis  r11, 0x8000                 */
    raw[3] = rlwinm(3, 0, 2, 0, 29);              /* slwi r0, r3, 2                   */
    raw[4] = dform(14, 11, 11, 0x2000);           /* addi r11, r11, 0x2000 (table)    */
    raw[5] = lwzx(12, 11, 0);                     /* lwzx r12, r11, r0                */
    raw[6] = mtctr(12);                           /* mtctr r12                        */
    raw[7] = BCTR;                                /* 0x8000001C: bctr                 */
    raw[8] = dform(14, 3, 0, 0xAA);               /* 0x80000020 case 0                */
    raw[9] = BLR;
    raw[10] = dform(14, 3, 0, 0xBB);              /* 0x80000028 case 1                */
    raw[11] = BLR;
    raw[12] = dform(14, 3, 0, 0xCC);              /* 0x80000030 case 2                */
    raw[13] = BLR;
    raw[14] = NOP;
    raw[15] = BLR;
}
static void put_be32(u8* p, u32 v) { p[0] = (u8)(v >> 24); p[1] = (u8)(v >> 16); p[2] = (u8)(v >> 8); p[3] = (u8)v; }

static void test_jump_tables(void) {
    const char* T = "jump_tables";
    u32 raw[JT_N];
    jt_program(raw);
    /* 4 entries (cmplwi 3): three local cases and one target outside this chunk */
    u8 table[16];
    put_be32(table + 0, BASE + 0x20u);
    put_be32(table + 4, BASE + 0x28u);
    put_be32(table + 8, BASE + 0x30u);
    put_be32(table + 12, 0x80100000u);
    emit_add_data_section(TABLE_ADDR, sizeof(table), table);

    char* off = emit_text(raw, JT_N);
    expect(T, strstr(off, "switch (target)") == NULL, "off by default: the dispatcher handles the bctr");

    emit_set_jump_tables(true);
    char* on = emit_text(raw, JT_N);
    char* blk = block_of(on, "// 8000001C: bctr");
    expect(T, blk && strstr(blk, "switch (target) {"), "bctr becomes a switch");
    expect(T, blk && strstr(blk, "case 0x80000020u: goto label_80000020;"), "case 0 is a local goto");
    expect(T, blk && strstr(blk, "case 0x80000028u: goto label_80000028;"), "case 1 is a local goto");
    expect(T, blk && strstr(blk, "case 0x80000030u: goto label_80000030;"), "case 2 is a local goto");
    expect(T, blk && !strstr(blk, "0x80100000u:"), "a target outside the chunk is not a case");
    expect(T, blk && strstr(blk, "default: break;") && strstr(blk, "ctx->pc = target;") &&
                  strstr(blk, "DOLRECOMP_RETURN;"), "anything else still goes to the dispatcher");

    /* a hooked PC must keep reaching the dispatcher */
    emit_add_dispatch_pc(BASE + 0x28u);
    char* hooked = emit_text(raw, JT_N);
    char* hb = block_of(hooked, "// 8000001C: bctr");
    expect(T, hb && !strstr(hb, "case 0x80000028u:"), "a --dispatch-pc target is not a case");
    emit_set_jump_tables(false);
    free(off); free(on); free(blk); free(hooked); free(hb);
}

/* ---- --indirect-calls ------------------------------------------------------------------------------- */
static void test_indirect_calls(void) {
    const char* T = "indirect_calls";
    /* 0: mtctr r12 ; 4: bctrl ; 8: li r3, 1 ; C: blr */
    u32 raw[] = {mtctr(12), BCTRL, dform(14, 3, 0, 1), BLR};
    emit_set_direct_calls(true);
    char* off = emit_text(raw, 4);
    expect(T, strstr(off, "DOLRECOMP_INDIRECT_CALL(") == NULL, "off by default");
    emit_set_indirect_calls(true);
    char* on = emit_text(raw, 4);
    char* blk = block_of(on, "// 80000004: bctrl");
    expect(T, blk && strstr(blk, "ctx->lr = 0x80000008u;"), "the link register is set first");
    expect(T, blk && strstr(blk, "DOLRECOMP_INDIRECT_CALL(target, 0x80000008u, label_80000008);"),
           "bctrl calls natively and resumes at its return address");
    expect(T, blk && strstr(blk, "DOLRECOMP_RETURN;"), "a miss still returns to the dispatcher");
    char* h = header_text();
    expect(T, strstr(h, "#define DOLRECOMP_INDIRECT_CALL(") != NULL, "the header defines the call");
    expect(T, strstr(h, "target != 0x80000028u") != NULL, "hooked PCs are excluded at run time");
    emit_set_direct_calls(false);
    char* nodirect = emit_text(raw, 4);
    expect(T, strstr(nodirect, "DOLRECOMP_INDIRECT_CALL(") == NULL, "needs --direct-calls");
    emit_set_indirect_calls(false);
    free(off); free(on); free(blk); free(h); free(nodirect);
}

/* ---- --function-overhang ---------------------------------------------------------------------------- */
static void test_function_overhang(void) {
    const char* T = "function_overhang";
    /* window = 4 instructions; past the edge: b +8 (cannot fall through), then code, then a function at edge+0x18 */
    enum { N = 12 };
    u32 raw[N] = {NOP, NOP, NOP, NOP,
                  branch(BASE + 0x10u, BASE + 0x18u), NOP, dform(14, 3, 0, 1), NOP, NOP, BLR, /* 0x24: next fn */
                  NOP, BLR};
    PPCInst insts[N];
    for (u32 i = 0; i < N; i++) insts[i] = ppc_decode(raw[i], BASE + i * 4u);
    u32 starts[] = {BASE, BASE + 0x28u};

    emit_set_chunk_overhang(512);
    expect(T, emit_overhang_length(insts, 4, N - 4, NULL, 0) == 1u, "default rule: stops after the b");
    emit_set_function_starts(starts, 2);
    emit_set_function_overhang(64);
    expect(T, emit_overhang_length(insts, 4, N - 4, NULL, 0) == 6u, "runs to the next function start (0x28)");
    emit_set_function_overhang(4);
    expect(T, emit_overhang_length(insts, 4, N - 4, NULL, 0) == 1u, "over the budget: the default rule");
    emit_set_function_overhang(64);
    emit_add_dispatch_pc(BASE + 0x1Cu);
    expect(T, emit_overhang_length(insts, 4, N - 4, NULL, 0) == 1u, "a hooked PC inside: the default rule");
    emit_set_function_overhang(0);
    emit_set_function_starts(NULL, 0);
}

int main(void) {
    test_loop_budget();
    test_dynamic_loop_budget();
    test_jump_tables();
    test_indirect_calls();
    test_function_overhang();
    if (failures) {
        fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    printf("dispatch_paths: all passed\n");
    return 0;
}
