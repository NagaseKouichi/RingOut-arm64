# The GameCube SDK as a reference, via zeldaret/tp

SoulCalibur II links the Nintendo GameCube SDK, and this recompiler executes
that SDK's PowerPC along with the game's own. When a question is "what is this
guest code actually doing", a *matching decompilation of any game sharing the
SDK* is ground truth.

Two are useful, for different halves:

| source | what it covers | use it for |
|---|---|---|
| [`zeldaret/tp`](https://github.com/zeldaret/tp) `libs/dolphin/src` | 34 SDK modules | almost everything below |
| `ttyd-main` `libs/dolsdk2004` (surveyed 2026-08-05) | includes `THPDec.c` | THP video decode — **TP has no THP module** |

TP is **CC0**, so reading and quoting it carries no licence friction. Do not
vendor it: nothing here needs to be compiled, only read.

## Module map — which one answers which question

`libs/dolphin/src` contains:

```
G2D ai am amcnotstub amcstubs ar ax axart axfx base card db demo dsp dtk dvd
exi fileCache gd gf gx hio mcc mix mtx odemustubs odenotstub os pad perf seq
si sp support syn texPalette vi
```

The ones that map onto work in this project:

- **`mtx`** — `mtx.c` has the full `PSMTX*` set (Concat, ConcatArray, Inverse,
  InvXpose, Quat, RotRad/RotTrig/RotAxisRad, Scale/ScaleApply, Trans/TransApply,
  Transpose, Copy, Identity, Reflect), 187 paired-single ops. `psmtx.c` has the
  array/skinning variants. `mtxvec.c`, `mtx44.c`, `quat.c`, `vec.c` alongside.
  This is the reference for unquantised (type 0) paired-single traffic.
- **`dsp` + `ax`** — DSP task queueing and the AX audio pipeline. Relevant to
  anything about audio timing and netplay determinism.
- **`os`** — `OSCache.c` (dcbz/flush/invalidate semantics, which this core
  emulates), `OSContext.c`, `OSAlarm.c`, `OSInterrupt.c`, `OSLink.c` (REL
  loading — SC2 ships none), `OSMemory.c`.
- **`gx`** — display list and command building, behind the write-gather pipe.

## What this pass established

### The paired-single matrix library is NOT worth intercepting

The idea was: HLE the hot `PSMTX*` routines with native SIMD instead of
emulating them, reusing the hook machinery that already intercepts the movie
player (`StaticRecompCore_Run.cpp`, the six `--dispatch-pc` addresses).

**Measured and dead.** A gameplay profile (`-g1` module, `task-clock` sampling
delayed past boot and menus, 16000 frames of arcade-match, mapped to guest PCs
by `.github/scripts/hot-guest-code.sh`) put the paired-single-dense share of
hot code at **0.00%**. 39 of the top 40 guest PCs are integer/logic; the single
mixed-FP entry is 0.11%.

Gameplay concentrates in one region instead:

```
0x8000C600-0x8000DBFF cluster    9.60%   (33 of the 39 mapped entries)
everything else in the top 40    0.83%
(chunk entry switch)             4.45%   <- dispatcher, not guest work
```

That cluster is the game's own RNG at `0x8000DA84` (Schrage's method), already
closed twice: not vectorisable, and the 64-bit modmul shortcut is not bit-exact.

**The trap that motivated the idea.** The "paired singles are 39% of the CPU
thread" figure in `work/perf-fmv/RESULTS.md` is from **FMV profiles**, captured
while a movie played — that is THP video decode, and its dynamic counts show
stores dominated by quantised types 4 and 7. Gameplay has a completely
different instruction mix. Do not carry an FMV number into a gameplay argument.

Reproduce with `work/ringout-perf/ab/probe-matrix.sh` and `classify-hot.py`.

### The host-dependent DSP thread is fixed, and the fix is complete

Upstream Dolphin derived `MAIN_DSP_THREAD` from the host's core count, which
made an emulation setting depend on the machine — two netplay peers with
different CPUs would run different DSP emulation, with nothing syncing it
(`NetPlayServer` sends `dsp_hle` and `dsp_enable_jit` and nothing else).

That override is **removed** in the shipped runtime
(`ModernGekko/vendor/dolphin/Source/Core/Core/Core.cpp:583`, which now explains
why), and the value is pinned in
`ModernGekko/src/runtime/dolphin_runtime.cpp:475`:

```cpp
Config::SetBase(Config::MAIN_DSP_THREAD, false);
```

Verified 2026-09-20 on a 12-core machine, which upstream's rule would have set
to `true`: seeding a user directory with `DSPThread = True` and running 600
frames leaves the ini reading `DSPThread = False`. So the pin also defeats a
**stale install** carrying the old value — there is no residual hazard from an
old `Dolphin.ini`.

Note `RecompCore/Source/Core/Core/Core.cpp:612` still carries the upstream
override. RecompCore is a vendored upstream reference, not what
`moderngekko-run` is built from, so this is not a live defect — but do not
copy that file forward without re-applying the fix.

## What this reference cannot do

- **No symbol matching for SC2.** `orig/GZ2E01/` holds only a `.gitkeep` — the
  repo ships no binary, exactly the limitation `ttyd-main` had.
  `config/GZ2E01/symbols.txt` is 2.5 MB of symbols but they are *TP's*
  addresses, meaningless here.
- **No recompiler technique.** TP is a decompilation. Its `tools/`,
  `configure.py` and REL machinery are decomp build tooling.
- **No THP.** Use `ttyd-main/libs/dolsdk2004` for the video-decode half.

## How to use it

Read a module directly rather than cloning 285 MB:

```sh
gh api repos/zeldaret/tp/contents/libs/dolphin/src/mtx/mtx.c --jq .content \
  | base64 -d > /tmp/mtx.c
```

Identification of a routine in SC2 still has to be done the way the RNG at
`0x8000DA84` was: take a hot guest PC from a profile and compare its shape
against the SDK source by hand. A PS-density score is a *shape* test and is not
proof that a routine is an SDK function.
