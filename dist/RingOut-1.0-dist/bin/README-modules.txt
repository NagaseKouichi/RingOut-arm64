Both modules carry the constant RAM bound, the chunk-entry ram local
(MODULE_RAM_LOCAL) AND the unpacked CR (CPU ABI 4 -- they need a runtime
built from the same commit or later), with MODULE_PSQ_FAST and MODULE_MEM_FAST on and the
retrained GRSEAF profile. Check the CPU with: grep -o avx2 /proc/cpuinfo

gGRSEAF_recomp.so            -march=x86-64-v3, needs AVX2+FMA. Frame hash
                             4217c7669322 over 16000 frames of arcade-match --
                             the reference domain, identical to the modules the
                             published measurements were taken on. Use this one
                             unless the CPU cannot run it.
                             .text 27,107,924

gGRSEAF_recomp.v2-noavx2.so  -march=x86-64-v2, for a CPU without AVX2. PLAYS
                             CORRECTLY but is NOT hash-compatible: with no FMA
                             hardware the guest's fmadd family goes through
                             glibc's fma (472 calls) instead of 426 hardware
                             vfmadd, and the frame hash is 041916d52f4c.
                             Replays, savestates and netplay will not match a
                             v3/native build. No flag fixes this -- the
                             reference itself uses hardware FMA.
                             .text 27,201,280
                             Run it with:
                               ./RingOut --module <path>/gGRSEAF_recomp.v2-noavx2.so

Verified on both: 1 ram-field load per chunk (was 999), and the v2 build
contains 0 AVX2-family instructions.
