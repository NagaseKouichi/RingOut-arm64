# Ring Out — Linux ARM64

English · [中文](README-CN.md)

This repository is a **Linux aarch64 (ARM64) build fork** of [jackpoison-prog/RingOut](https://github.com/jackpoison-prog/RingOut).

It builds and runs Ring Out natively on 64-bit ARM Linux. It is **not** the official x86_64 package running through box64 or another x86 translator.

The fork currently tracks upstream **v1.6.3**. It carries the small ARM64 portability guards and reproducible cross-build / packaging scripts needed to produce native aarch64 packages.

**No game data or game-derived code is included.** Supply an image from a GameCube disc you own. The setup process accepts the formats supported upstream, including `.iso`, `.gcm`, and `.rvz`.

---

## Upstream and fork

- Upstream: <https://github.com/jackpoison-prog/RingOut>
- ARM64 fork: <https://github.com/NagaseKouichi/RingOut-arm64>
- Upstream's complete README: [`docs/README.upstream.md`](docs/README.upstream.md)
- Stand-alone portability patch: [`patches/0001-linux-aarch64-port-guards.patch`](patches/0001-linux-aarch64-port-guards.patch)

Game features, the Vulkan renderer, and the static recompilation pipeline are those of upstream. DolRecomp translates the GameCube PowerPC executable to portable C; the local host compiler then produces an aarch64 module.

The ARM64 delta deliberately stays small:

| File | Purpose |
|---|---|
| `DolRecomp/src/cpu/cpu.c` | Applies `fmod@GLIBC_2.2.5` only on x86_64; that legacy symbol version does not exist on aarch64 glibc. |
| `dist/RingOut-1.0-dist/module-src/deps/dolrecomp-src/cpu/cpu.c` | Keeps the desktop package's source copy in sync. |
| `dist/RingOut-1.0-dist/setup.sh` | Makes `--deck` explicitly x86_64-only and prevents use of the shipped x86 PGO profile on ARM64. |
| `dist/RingOut-1.0-dist/RingOut` | Enables the bundled `ld-linux-x86-64.so.2` fallback only on x86_64. |

To take later upstream updates, rebase the fork commits rather than applying the patch file again:

```sh
git fetch upstream
git rebase upstream/main
```

---

## Packages

| Package | Corresponds to upstream | Purpose |
|---|---|---|
| `RingOut-<version>-linux-aarch64.zip` | `linux-x86_64.zip` | ARM64 module build package: `setup.sh`, `dolrecomp`, `module-src`, and the ARM64 runtime. |
| `RingOut-<version>-armada-aarch64.zip` | `steamdeck-x86_64.zip` | Runtime-only package for armada and similar ARM64 handhelds. No build tools or disc-derived files. |

Neither zip includes `game/` or `bin/g*_recomp.so`; both are derived from your disc.

The `linux-aarch64` package also includes `source/`, the GPL corresponding-source shipment for the shipped ARM64 binaries. The smaller runtime package follows the upstream Deck packaging model and does not include build tooling or that source shipment.

### Runtime-only package workflow

1. On an aarch64 Linux system capable of building the module—or with the cross-build workflow below—extract the build package and run:

   ```sh
   ./setup.sh /path/to/your-disc.iso
   ```

   Do **not** use `--deck` on ARM64.

2. Copy the resulting `game/` directory and `bin/g<ID>_recomp.so` into the runtime package.
3. Run `./RingOut`.

The module must be an **ARM aarch64 ELF**. A module built from the official x86_64 package cannot be loaded on ARM64.

---

## Building the ARM64 runtime on an x86_64 host

The supported development path uses Docker, an aarch64 Debian 12 sysroot, and an `aarch64-linux-gnu` cross toolchain. This avoids compiling the large Dolphin-derived runtime under qemu-user.

```sh
# Prepare arm64/sysroot with the Debian 12 ARM64 headers and libraries.
./arm64/build-linux-aarch64.sh

# Create the two allow-listed packages; no disc files or modules are included.
./arm64/package-official-aarch64.sh
```

Packages are written to `release/`.

On a native aarch64 Fedora, Debian, or Ubuntu system, upstream `setup.sh` can build a personal module too. It requires `cmake`, `ninja`, `clang`, and `python3`; PGO training also needs `llvm-profdata`. On Fedora Atomic:

```sh
sudo rpm-ostree install cmake ninja-build clang python3 llvm
sudo systemctl reboot
```

Use `./setup.sh --pgo /path/to/your-disc.iso` to train a profile on the target ARM compiler. Do not use the included x86 `.profdata` files for an ARM module.

---

## Performance

The emulated PowerPC CPU workload is principally single-threaded. Dolphin dual-core mode separates CPU and video work, but it does not divide one guest CPU across all host cores. A system-wide low CPU percentage alongside one saturated core is expected for a CPU-bound scene.

Upstream 1.6.2 and 1.6.3 add portable generated-C module optimizations, so they apply to ARM64 when the disc is regenerated and the module rebuilt. They are not obtained by copying a new runtime over an old module. On ARM64, target-native PGO is generally more valuable than attempting to reuse an x86 profile; LTO can provide a further incremental improvement.

---

## Licence and disclaimer

The project is **GPL-2.0-or-later**; see [`LICENSE`](LICENSE). This is an unofficial fan project, unaffiliated with Bandai Namco, Dolphin, or the upstream projects. Do not request or distribute disc images, extracted `game/` data, generated source chunks, or recompiled modules.