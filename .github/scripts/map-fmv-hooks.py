#!/usr/bin/env python3
"""Map the six FMV-HLE hook PCs onto JP and PAL by voting over mid-function slices.

The first attempt keyed on the function prologue and failed: `stwu r1,-N(r1)` /
`mflr r0` is generic, one JP prefix had 297 matches, and a 16-byte "hit" on PAL
gave a delta inconsistent with its neighbours -- a coincidence, not the function.

This takes several slices from INSIDE each function instead. A slice at offset K
that matches at address A implies the function starts at A-K, so every slice
votes for a start address. Slices containing an embedded absolute address (which
differs between discs) simply fail to match and cast no vote, rather than
corrupting the answer. A start address that several independent slices agree on,
and that looks like a function entry, is a real mapping.

Reports addresses only; copies nothing out of any DOL.

--masked adds a second pass for a library that was REARRANGED rather than
relocated (JP). Moving functions changes the displacement of every `bl` between
them, and a 32-byte slice nearly always holds one, so a byte-exact slice cannot
match -- which is why JP drew 1-5 votes. The masked pass compares instructions
with the fields that legitimately differ between builds blanked out: branch
displacements, `lis`/`addis` and `addi`/`ori` immediates (absolute address
halves), and small-data offsets off r2/r13. What is left -- opcodes, registers,
struct offsets, compare constants -- still has to agree across the whole slice.
A rearranged library has no shared delta, so this pass accepts per function
and reports, as its validator, whether pairs that are adjacent in the US build
keep their US spacing.
"""
import collections
import struct
import sys

HOOKS = [
    (0x8020C1E8, "mwPlyStartAfs"),
    (0x80209138, "mwPlyFxCnvFrmARGB"),
    (0x8020D3B8, "mwPlyExecSvrHndl"),
    (0x80207E90, "mwPlyIsNextFrmReady"),
    (0x80207EE8, "mwPlyRelCurFrm"),
    (0x80208244, "getfrm"),
]
DISCS = {
    "US": "work/gameroot/sys/main.dol",
    "JP": "work/gameroot_jp/sys/main.dol",
    "PAL": "work/gameroot_pal/sys/main.dol",
    "Plus": "work/plus_root/sys/main.dol",
}
# Offsets into the function to slice at, and slice widths. Offset 0 is excluded
# deliberately -- that is the generic prologue that produced the false hits.
OFFSETS = (16, 24, 32, 40, 48, 64, 80, 96, 112, 128, 160, 192)
WIDTHS = (32, 48)


def sections(path):
    blob = open(path, "rb").read()
    offs = struct.unpack(">7I", blob[0x00:0x1C])
    addrs = struct.unpack(">7I", blob[0x48:0x64])
    sizes = struct.unpack(">7I", blob[0x90:0xAC])
    return [(a, s, blob[o:o + s]) for o, a, s in zip(offs, addrs, sizes) if s and a]


def read_at(secs, addr, n):
    for a, s, data in secs:
        if a <= addr < a + s and addr - a + n <= s:
            return data[addr - a: addr - a + n]
    return None


def find_all(secs, pat):
    out = []
    for a, s, data in secs:
        i = data.find(pat)
        while i >= 0:
            out.append(a + i)
            i = data.find(pat, i + 1)
    return out


def mask_word(w):
    """Blank the fields of one PowerPC instruction that differ between builds."""
    op = w >> 26
    ra = (w >> 16) & 31
    if op == 18:                      # b / bl: keep opcode, AA, LK
        return w & 0xFC000003
    if op == 16:                      # bc: keep BO/BI, AA, LK
        return w & 0xFFFF0003
    if op == 15:                      # lis / addis: an address half
        return w & 0xFFFF0000
    if op in (14, 24) and ra != 0:    # addi rD,rA,lo / ori: an address half
        return w & 0xFFFF0000
    if 32 <= op <= 55 and ra in (2, 13):  # D-form load/store off a small-data base
        return w & 0xFFFF0000
    return w


def masked_words(data):
    n = len(data) // 4
    return [mask_word(x) for x in struct.unpack(f">{n}I", data[:n * 4])]


def masked_index(secs):
    """(address, masked words) per section, plus a 2-word key -> word index."""
    out = []
    for a, s, data in secs:
        words = masked_words(data)
        key = collections.defaultdict(list)
        for i in range(len(words) - 1):
            key[(words[i], words[i + 1])].append(i)
        out.append((a, words, key))
    return out


def find_all_masked(index, pat_words):
    hits = []
    k = (pat_words[0], pat_words[1])
    for a, words, key in index:
        for i in key.get(k, ()):
            if words[i:i + len(pat_words)] == pat_words:
                hits.append(a + i * 4)
    return hits


def vote(us, addr, finder):
    votes = collections.Counter()
    tried = 0
    for off in OFFSETS:
        for w in WIDTHS:
            pat = read_at(us, addr + off, w)
            if not pat or len(pat) < w:
                continue
            tried += 1
            hits = finder(pat)
            # A slice that matches everywhere is noise; one that matches a
            # handful of places still votes, and agreement decides.
            if 0 < len(hits) <= 4:
                for h in hits:
                    votes[h - off] += 1
    return votes, tried


def masked_pass(us, target_secs, target):
    """Per-function masked vote, for a library that was rearranged."""
    print(f"  -- masked pass (branch displacements and address halves blanked)")
    index = masked_index(target_secs)
    got = {}
    for addr, label in HOOKS:
        votes, tried = vote(us, addr, lambda pat: find_all_masked(index, masked_words(pat)))
        if not votes:
            print(f"  {label:22s} NO VOTES from {tried} slices")
            continue
        best, n = votes.most_common(1)[0]
        runner = votes.most_common(2)[1][1] if len(votes) > 1 else 0
        entry = looks_like_entry(target_secs, best)
        # Stricter than the exact pass: a masked slice is easier to match by
        # coincidence, so it needs twice the margin over the runner-up.
        ok = n >= 4 and n >= 2 * max(runner, 1) and entry
        print(f"  {label:22s} 0x{best:08X}  votes {n}/{tried}"
              f"  runner-up {runner}  entry {'yes' if entry else 'NO'}"
              f"  delta {best - addr:+#x}   {'ACCEPT' if ok else 'reject'}")
        if ok:
            got[label] = best
    # Validator: functions adjacent in the US build usually stay adjacent when
    # a library is rearranged at object-file granularity, so their spacing
    # should survive even when the deltas do not match.
    us_at = dict((l, a) for a, l in HOOKS)
    ordered = sorted(HOOKS)
    for (a0, l0), (a1, l1) in zip(ordered, ordered[1:]):
        if l0 in got and l1 in got:
            same = got[l1] - got[l0] == a1 - a0
            print(f"  spacing {l0} -> {l1}: US {a1 - a0:#x}, {target} "
                  f"{got[l1] - got[l0]:+#x}  {'kept' if same else 'CHANGED'}")
    return got


def looks_like_entry(secs, addr):
    head = read_at(secs, addr, 8)
    if not head:
        return False
    w0, w1 = struct.unpack(">2I", head)
    return (w0 >> 16) == 0x9421 or w0 == 0x7C0802A6 or w1 == 0x7C0802A6


MASKED = False


def main():
    global MASKED
    MASKED = "--masked" in sys.argv[1:]
    secs = {}
    for name, path in DISCS.items():
        try:
            secs[name] = sections(path)
        except OSError as e:
            print(f"!! {name}: {e}")
    us = secs["US"]
    results = {}

    for target in ("JP", "PAL", "Plus"):
        if target not in secs:
            continue
        print("=" * 76)
        print(f"{target}: voting over mid-function slices")
        print("=" * 76)
        results[target] = {}
        for addr, label in HOOKS:
            votes, tried = vote(us, addr, lambda pat: find_all(secs[target], pat))
            if not votes:
                print(f"  {label:22s} NO VOTES from {tried} slices")
                continue
            best, n = votes.most_common(1)[0]
            runner = votes.most_common(2)[1][1] if len(votes) > 1 else 0
            entry = looks_like_entry(secs[target], best)
            ok = n >= 3 and n > runner and entry
            print(f"  {label:22s} 0x{best:08X}  votes {n}/{tried}"
                  f"  runner-up {runner}  entry {'yes' if entry else 'NO'}"
                  f"  delta {best - addr:+#x}   {'ACCEPT' if ok else 'reject'}")
            if ok:
                results[target][label] = best

        # The library is one blob, so genuine mappings should cluster: either a
        # single shared delta, or a small number of them. Scattered deltas mean
        # coincidences got through.
        got = results[target]
        if got:
            deltas = collections.Counter(got[l] - a for a, l in HOOKS if l in got)
            print(f"  delta clustering: " +
                  ", ".join(f"{d:+#x} x{c}" for d, c in deltas.most_common()))
        print(f"  mapped {len(got)} of {len(HOOKS)}")
        if MASKED and len(got) < len(HOOKS):
            masked = masked_pass(us, secs[target], target)
            # Where the exact pass accepted a function, it wins: it is stronger.
            for l, a in masked.items():
                if l in got and got[l] != a:
                    print(f"  !! {l}: masked 0x{a:08X} disagrees with exact 0x{got[l]:08X}")
                got.setdefault(l, a)
            print(f"  mapped {len(got)} of {len(HOOKS)} with the masked pass")
        if len(got) == len(HOOKS):
            line = " ".join(f"--dispatch-pc 0x{got[l]:08X}" for _, l in HOOKS)
            print(f"\n  COMPLETE -- setup flags for {target}:\n  {line}")
        print()
    return 0


if __name__ == "__main__":
    sys.exit(main())
