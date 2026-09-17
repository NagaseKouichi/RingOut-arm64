# Measuring things here

Most of the wrong turns in this project were not bad ideas. They were good
measurements of the wrong thing. This page is the accumulated defence.

## The harness

`.github/scripts/module-bench.sh <package> <frames> <input> <reps> <tag>=<so>…`

Runs each module over the **same emulated frames**, driven by a frame-keyed
input script, counted in **retired cycles** (`perf stat -e cycles:u`) rather
than wall time. Because every arm executes identical work, wall time *is*
comparable too, and no measurement window has to be aligned with anything.

Properties that matter, each of which exists because its absence produced a
wrong answer:

* **Reps alternate, and the order reverses on even reps.** A result that tracks
  run order rather than the module is then visible instead of averaged away.
* **Every run gets a fresh user directory** seeded from the package, so no run
  inherits state the previous one wrote.
* **A short run is discarded, not scored.** Without that check a module that
  crashes at frame 300 wins every comparison.
* **The hash column is the control.** Identical hashes mean the arms did the
  same emulated work. A layout-only change *must* match; a codegen change need
  not, but a changed hash means the comparison is measuring different content.

## What the workload has to be

`.github/input-scripts/arcade-match.txt` drives an actual Arcade match. Frames
before ~4500 are boot and menus, so use a frame count where gameplay dominates.

Three workloads that were used before this one and were all wrong:

* **Boot and an idle menu.** Carries ~0.1% of a session's paired-single traffic.
  A change can look free there and cost 3% in a match.
* **Attract mode.** Reaches gameplay, but is *not reproducible across launches*
  — two runs of the same package put the intro movie 33 s apart and played the
  attract items in a different order, so a fixed window lands on different
  content each time.
* **A "reproducible" screen that was a dialog.** GX draw calls flat at 32/frame
  gave the game away. See the draw-call check below.

## Prove the run did what you think

The single most expensive recurring mistake here: a run finishes, the result
looks clean, and it is about *something else*. A day of per-stage analysis once
went into a route that never left the first fight.

Cheap checks, in the order they are worth doing:

* **Frames actually hashed** vs frames requested.
* **`RINGOUT_GX_STATS=<n>`** prints mean per-frame GX counters to stderr. This
  is how you tell gameplay from a menu: **~1–3 draw calls per frame parked on a
  dialog, 84–260 in a match.** A profile, a benchmark or a training run that
  never left a dialog looks entirely normal without it.
* **Save data exists.** No save means the game parks on a memory-card dialog and
  the whole run is wasted. Packages ship none by design.
* **The harness is armed.** `RINGOUT_DETERMINISM_LOG` enables the frame-keyed
  input *and* the frame limit; neither works without it. `_NOHASH=1` turns it
  into a performance harness — input and frame counter kept, per-frame hash
  dropped (the hash is ~29% of cycles and will dominate a profile).
* **Under `_NOHASH=1` the hash column is gone, so do not use the frame log as
  the work-identity control.** Every line is zeros; two logs of the same length
  are then byte-identical whatever the arms actually ran, and comparing their
  digests "passes" while proving nothing. Use the shutdown line instead:
  `fallback=` and `native_exc=` should agree across arms (0.5% on the Windows
  runs below), while `native=` is *expected* to differ — that is the lever.
* **Never conclude a step finished from an artifact's existence, size or
  mtime.** A stable file size across a few seconds was read as "the link
  finished"; the file was three days old, copied in from a previous install by
  `robocopy`. Idle build processes and a leftover `.ninja_lock` were likewise
  read as a stall on a run that had already printed "Setup complete". Read the
  completion line, or an exit code written by the step itself.

## Wall time on a machine that cannot hit the frame cap

The fixed-work harness runs headless with no frame limiter, so **wall time over
a fixed frame count is a valid speed measure** — useful where `perf` is not
available. Verified on the Windows test laptop (AMD A4-9125, 2 cores, 3.9 GB),
same runtime, same extracted disc, same route, 6000 frames, only the module
swapped, three alternating pairs:

| module | reps | median |
| --- | --- | --- |
| 09/13 build, before the perf work | 115.51 / 117.41 / 113.87 s | 115.51 s |
| 09/16 build, all of it | 78.82 / 79.04 / 79.40 s | 79.04 s |

**31.57% faster**, ranges nowhere near overlapping, and the spread is tighter
than the desktop table above (0.6 s across the new arm). Dispatches fell
358 M → 154 M. It also answered a live question: a profile trained under
clang 22 was **accepted and useful under the package's clang 23**
(`MODULE_PGO_USABLE - Success`), rather than silently missing as
`docs/profile-guided-optimisation.md` warns can happen across versions.

## Machine noise

Measure on an idle machine, and say which machine.

A build-time table taken on a loaded desktop had every row inflated — the same
clean build read 3m04s–3m24s there against 144s idle — and the noise had
*structure*: it looked like a 7% linker win, then a 33% one, depending which
pair you compared. If a measurement disagrees with a recorded figure by tens of
seconds, suspect the machine before the code.

Observed run-to-run spread on the gameplay benchmark, idle:

| machine | spread over 6–8 reps |
| --- | --- |
| desktop, Zen 3 | 0.7–1.8% |
| Steam Deck, Zen 2 | 0.5–0.8% |

Report `n`, min, max and spread alongside the mean, and say whether the arms'
ranges overlap. A 1% difference between two arms whose ranges overlap is not a
result; a 14% difference with a clean gap is.

## Attributing module cycles to constructs

`perf report -s srcline` on a `-DMODULE_DEBUG_LINES=ON` (`-g1`) module resolves
samples to a line of emitted C; `.github/scripts/construct-costs.py` buckets
those lines by what the line *does*. Three things that make the raw output
misleading, each of which produced a wrong reading first:

* **Filter to the module's own DSO.** Unfiltered, the biggest bucket was
  "unresolved" at 21.5% — samples in the runtime, libc and Dolphin's JIT, which
  cannot be ranked against module constructs. `--dsos=g<ID>_recomp.so` drops
  that to 1.3%. The split itself is worth printing: module 71.9%, runtime 22.1%,
  Jit64 2.2% (the fallback path), libc 1.4%.
* **Inlined helpers are charged to their own file:line**, not to the chunk line
  that used them, so chunk lines alone are well under half the samples. Read
  both halves or memory access looks free.
* **CR work splits across the store and the locals that compute it.** Bucketing
  only `ctx->cr` put CR at 3.3%; counting `cr_bits`/`cr_value` too put it at
  8.8%, the second-largest construct.

Also verify `-g1` changed nothing: the `-g1` and normal builds of the same tree
had byte-identical `.text` (29,910,448), so the profile describes what ships.

## A step count is not a cost, twice over

SC2 Plus looked like it had a large, easy win: `fallback=86.0 M` interpreted
steps against `native=1140.8 M` (7.5%), where JP is 0.69%. Two theories, both
measured, both worth less than the step count implied.

* **Its data-marked code: DEAD.** `work/plus_dol_doctored/main.dol` reclassifies
  two sections the Plus DOL calls DATA as TEXT (0x8024eca0, 35 KB; 0x80257680,
  382 KB), keeping offset/address/size identical so the loaded image is
  byte-identical — a build-time artifact only; players still boot the real disc.
  It yields 160 chunks instead of 133 and 198862 entries instead of 182439, and
  it is worth **+0.14% cycles with overlapping ranges, i.e. nothing.** The
  counters say why: the doctored build's dispatch count was *exactly* the
  baseline's (1,140,647,076, ten matching digits) and fallback moved **4 steps
  out of 86 million**. That 417 KB is never executed — it really is data. The
  only lasting effects were `smc_failed` 3 → 4 (the game writes one of those
  regions, so its chunk now fails verification) and +2.87 MB of dead code.
* **Its 3 self-patched chunks: worth ~3%, not 7.5%.** Plus patches base text at
  runtime, so those chunks fail SMC verification and run interpreted for good
  (identical `smc_failed=3` in every Plus arm; US/JP/PAL are 0). But profiling
  puts every interpreter-side symbol together at only 2.4–2.8% of cycles —
  ReadInstruction 0.99, SingleStepInner 0.63, Read_Opcode 0.40,
  SetPPCStateFromGuestState 0.40, Memcheck 0.34. Recovering it would mean
  recompiling from post-boot RAM: a large feature for ~3%.

Same shape as `--tail-calls`, which removed 37% of dispatches and cost 4.3%
cycles. Counting events is a way to find candidates, never a way to price them.

## Prefer a static gate to a dynamic one you cannot trigger

`--direct-calls` must not let a call bypass a PC the run loop hooks, and the
obvious check is to watch the hook fire. On the US disc that works: boot
headless with no determinism harness (the harness *disables* the FMV takeover,
which made the first attempt vacuous) and the intro movie fires `mwPlyStartAfs`
within ~70 s.

On SC2 Plus it cannot be done at all. 240 s headless, on both the shipped and
the new module, produced no hook line, and the runtime's PC histogram
(`STATICRECOMP_FMV_HIST`) dumped **48 windows over 1.92 G dispatches without one
bucket in the movie library's range** — Plus never reaches a movie that way. Note
also that a 16000-frame route run proves nothing here: it presses START through
the intro, so the hook does not fire on *any* disc, US included.

The static check is stronger anyway, because it does not depend on reaching the
code. In the generated tree, for each hooked PC assert that it is **never the
target of a direct call** (`ctx->pc = 0x…u;` followed by
`dolrecomp_call_enter`), that it **has an entry-switch case**, and that it
appears in `generated_entries.txt` (bare hex, no `0x` prefix — a grep expecting
one silently reports zero). Then mutation-test it: run the same check against a
PC that *is* a direct-call target and confirm it reports a non-zero count, or
the zeros prove nothing. On Plus: all six hooked PCs clean, against 25,250 real
call sites in the same tree.

## A constant can cost more than the load it replaces

The hottest line in the module is the inlined 32-bit RAM accessor (7.5% of all
samples), and the disassembly shows why: `cpu->ram` and `cpu->ram_size` are
**reloaded on every access** — 999 of each against ~999 accesses in
`func_8000D940` — because a store through `cpu->ram` may alias the struct
fields themselves.

Replacing the bound with the retail MEM1 literal removed all 999 `ram_size`
loads and made that chunk 8% smaller in instructions. It was **+7.53% cycles**
(232.84 → 250.37 G, clean gap, hash-identical). Instructions moved only +0.53%:
the literal let clang do range reasoning on `addr - 0x80000000 <= 0x17FFFFFC`
and duplicate paths, quadrupling comparisons in the chunks that grew
(2,588 → 9,556 in one) and taking `.text` from 29.9 MB to 54.1 MB. The load was
*helping* — it kept the check one opaque, unanalysable compare.

So: removing work from the hot line is not the same as making it faster, and
this is the third lever killed by code growth rather than by instruction count
(full paired-single inlining was +25% size for +5.46%). It also does not
condemn caching the base and bound in chunk-entry **locals**, which stay
variables and so buy no range reasoning — that variant is untested.

A note on the harness: the first overlap check only tested
`max(variant) >= min(base)`, which is trivially true when the variant is
*slower*, and it called a clean 7.53% loss "not a result". Test both directions.

## Instrument choice

* **Retired cycles** for "is this faster over fixed work".
* **Instructions** as the less noisy companion — it moves for codegen changes
  and barely moves for scheduling noise.
* **Guest-level attribution** (`-g1` plus `hot-guest-code.sh`) for "where does
  the time go inside the recompiled module".
* **PMU attribution** for "is this front-end, back-end or dispatch bound".
  Established here: front-end starvation 2.4% of cycles, indirect mispredicts
  1.3%, back end saturated. Only codegen volume is left.

Profiling with the determinism hash left on once put 28.6% of cycles in
`crc32_fold_pclmulqdq` — the harness measuring itself. `RINGOUT_DETERMINISM_NOHASH=1`.

## When a check is added, break it on purpose

Every guard in the build scripts was mutation-tested: reintroduce the bug and
confirm the check fails. A guard that has never failed is not known to work —
the launcher's glibc floor and the packaging assertions were both verified this
way, and the PGO draw-call guard proved itself by catching a real bug on its
first run.
