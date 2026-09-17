#!/usr/bin/env python3
"""Attribute module cycles to emitted-C constructs.

Usage: construct-costs.py <perf-report-srcline.txt> <generated-tree> [cyc_per_frame]

`perf report -s srcline` resolves samples inside a chunk to a file:line in the
emitted C. Two things make the raw output hard to read, and this script exists
for both:

  1. Inlined helpers are charged to THEIR OWN file:line (cpu.h, types.h,
     generated.h, cpu.c), not to the chunk line that called them. So chunk
     lines alone account for well under half the samples -- read both halves.
  2. A chunk line is meaningless on its own ("chunk_0097.c:120411"). The line's
     TEXT says what construct it is, so we read the generated C and bucket by
     what the line actually does.

Percentages are of total module samples. Pass the measured cycles/frame to also
print M cycles/frame per bucket, which is what earlier notes are recorded in.
"""
import collections
import os
import re
import sys

# Ordered: the first pattern that matches a line's text wins, so put the
# specific constructs ahead of the generic register traffic.
CHUNK_RULES = [
    ("entry switch",        (r"switch\s*\(\s*ctx->pc", r"^\s*case\s+0x")),
    ("pc bookkeeping",      (r"ctx->pc\s*=",)),
    ("downcount",           (r"downcount",)),
    ("direct/self call",    (r"\bfunc_[0-9a-fA-F]+\s*\(", r"dolrecomp_call_(enter|leave)")),
    ("paired-single",       (r"psq_", r"ps_(mul|add|sub|madd|msub|merge|sel|neg|abs|res|rsqrte)", r"dolrecomp_ps_round")),
    ("memory access",       (r"mem_(read|write)\d+", r"write_be\d+", r"read_be\d+")),
    # CR work splits across the store (ctx->cr) and the locals that compute it
    # (cr_bits / cr_value); without the locals it lands in "other chunk line"
    # and the CR total reads far too low.
    ("CR compute/branch",   (r"ctx->cr", r"cr_bits", r"cr_value", r"PPC_CR_", r"dolrecomp_cmp")),
    ("FP arithmetic",       (r"ctx->fpr", r"dolrecomp_force25", r"\(f32\)", r"\(f64\)")),
    ("XER / carry",         (r"ctx->xer", r"\bca\b", r"carry")),
    ("FPRF/status",         (r"fprf", r"PPC_FPRF", r"g_fprf")),
    ("rotate/mask",         (r"rotl32", r"rotl64", r"dolrecomp_mask")),
    ("GPR arithmetic",      (r"ctx->gpr",)),
]

HELPER_BUCKETS = {
    "cpu.h":       "helper: memory/inline (cpu.h)",
    "types.h":     "helper: byteswap (types.h)",
    "cpu.c":       "helper: out-of-line (cpu.c)",
    "generated.h": "helper: emitted inline (generated.h)",
    "psq_inline.h": "helper: paired-single inline",
}


def parse_report(path):
    """Yield (percent, file, line) from `perf report -s srcline --stdio`."""
    # Lines look like:  " 3.70%  chunk_0097.c:120411"  (or a bare address)
    pat = re.compile(r"^\s*(\d+\.\d+)%\s+(.*?)\s*$")
    for raw in open(path, errors="replace"):
        if raw.lstrip().startswith("#") or not raw.strip():
            continue
        m = pat.match(raw)
        if not m:
            continue
        pct, loc = float(m.group(1)), m.group(2).strip()
        if ":" not in loc:
            yield pct, loc, None
            continue
        fn, _, ln = loc.rpartition(":")
        try:
            yield pct, os.path.basename(fn), int(ln)
        except ValueError:
            yield pct, os.path.basename(fn), None


def line_text(tree, fname, lineno, cache={}):
    if lineno is None:
        return ""
    key = fname
    if key not in cache:
        found = None
        for root, _, files in os.walk(tree):
            if fname in files:
                found = os.path.join(root, fname)
                break
        cache[key] = open(found, errors="replace").read().splitlines() if found else []
    lines = cache[key]
    return lines[lineno - 1] if 0 < lineno <= len(lines) else ""


def classify(fname, text):
    for base, bucket in HELPER_BUCKETS.items():
        if fname == base:
            return bucket
    if not text:
        return "unresolved (no line text)"
    for name, pats in CHUNK_RULES:
        for p in pats:
            if re.search(p, text):
                return name
    return "other chunk line"


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    report, tree = sys.argv[1], sys.argv[2]
    cpf = float(sys.argv[3]) if len(sys.argv) > 3 else None

    buckets = collections.Counter()
    examples = collections.defaultdict(list)
    total = 0.0
    for pct, fname, lineno in parse_report(report):
        total += pct
        text = line_text(tree, fname, lineno) if lineno else ""
        b = classify(fname, text)
        buckets[b] += pct
        if text and len(examples[b]) < 3:
            examples[b].append(f"{fname}:{lineno}  {text.strip()[:90]}")

    print(f"total samples accounted: {total:.1f}%"
          + (f"   (scaled to {cpf:.1f} M cycles/frame)" if cpf else ""))
    print("=" * 78)
    for b, pct in buckets.most_common():
        line = f"{pct:6.2f}%  {b}"
        if cpf:
            line += f"   {pct / 100 * cpf:6.2f} M cyc/frame"
        print(line)
    print("=" * 78)
    print("\nexample lines per bucket (sanity-check the classification):")
    for b, _ in buckets.most_common(8):
        print(f"\n  [{b}]")
        for e in examples[b]:
            print(f"    {e}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
