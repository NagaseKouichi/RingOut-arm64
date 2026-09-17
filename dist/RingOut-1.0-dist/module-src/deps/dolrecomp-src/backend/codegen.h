#ifndef DOLRECOMP_BACKEND_CODEGEN_H
#define DOLRECOMP_BACKEND_CODEGEN_H

#include "common/types.h"
#include "frontend/decoder.h"
#include "backend/emitter.h"
#include "analysis/code_section.h"

#define EMIT_CHUNK_INSTRUCTIONS 4096u

/* MEASURED DEAD, do not rebuild it: cutting chunks on guest FUNCTION
 * boundaries (2196 functions in 120 files instead of 132 x 4096 instructions,
 * merging under 64 instructions and splitting over 4096). It generated
 * correctly -- identical emitted instruction labels, ascending addresses,
 * deterministic -- and cost **+62.1% cycles / +48.3% instructions** on the US
 * disc, no PGO, arcade match x3 (276.60 -> 448.49 G).
 *
 * The chunk being huge is load-bearing, and NOT for the reasons that are easy
 * to guess: the static shape barely moved (native call sites 25229 vs 25233,
 * local gotos 263149 vs 264176) and dispatches HALVED (346.7 M -> 163.9 M on
 * the VS route). Only ~3530 call sites changed from a self-recursive call into
 * the same C function to a call into another one. None of that accounts for
 * +343 G instructions, so the mechanism is UNEXPLAINED -- which is exactly why
 * a retry needs a new measurement, not a new theory. */

typedef struct {
    const PPCInst* insts;
    u32 count;
    u32 func_addr;
    char path[1200];
    char include_name[512];
} ChunkJob;

int run_chunk_jobs(const ChunkJob* jobs, u32 job_count, u32 requested_jobs);
u32 effective_chunk_jobs(u32 job_count, u32 requested_jobs);

#endif
