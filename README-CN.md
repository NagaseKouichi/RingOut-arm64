# Ring Out — Linux ARM64

[English](README.md) · 中文

本仓库是 [jackpoison-prog/RingOut](https://github.com/jackpoison-prog/RingOut) 的 **Linux aarch64（ARM64）编译分支**。

目标：在 Linux ARM64 上原生编译并运行 Ring Out，而不是用 box64 跑官方 x86_64 包。

上游官方发行只有 `linux-x86_64` 与 `steamdeck-x86_64`。本分支基于 **v1.5.2**，补上 ARM64 能编过、能打包所缺的守卫，并附带交叉编译脚本。

**不包含任何游戏数据或游戏代码。** 你必须提供自己拥有的 GameCube 光盘镜像（美版 Soulcalibur II 的 disc ID 为 `GRSEAF`）。

---

## 和官方的关系

- 上游：https://github.com/jackpoison-prog/RingOut
- 本 fork：https://github.com/NagaseKouichi/RingOut-arm64
- 相对上游只改了 4 个第一方文件（+23 / −5）。独立补丁：[`patches/0001-linux-aarch64-port-guards.patch`](patches/0001-linux-aarch64-port-guards.patch)
- 游戏功能、Vulkan 渲染、静态重编译流程与上游相同。客机 ISA 仍是 GameCube PowerPC；DolRecomp 生成可移植 C，由宿主编译器编成本机 aarch64。
- 官方 README（x86 / Steam Deck）全文见 [`docs/README.upstream.md`](docs/README.upstream.md)

相对上游的源码改动：

| 文件 | 作用 |
|---|---|
| `DolRecomp/src/cpu/cpu.c` | `fmod@GLIBC_2.2.5` 仅在 `__x86_64__` 上钉死（aarch64 glibc 没有该符号） |
| `dist/RingOut-1.0-dist/module-src/deps/dolrecomp-src/cpu/cpu.c` | 同上，桌面包里的拷贝 |
| `dist/RingOut-1.0-dist/setup.sh` | `--deck` 仅限 x86_64；随包 PGO 只在 x86_64 上使用 |
| `dist/RingOut-1.0-dist/RingOut` | 捆绑的 `ld-linux-x86-64.so.2` 只在 x86_64 上启用 |

官方更新后建议在本分支上 `git fetch upstream && git rebase upstream/main`。冲突只会出现在上述 4 个文件。

---

## 两种发行包（对齐官方格式）

| 包 | 对应官方 | 用途 |
|---|---|---|
| `RingOut-<ver>-linux-aarch64.zip` | `linux-x86_64.zip` | 在 ARM64 Linux 上编译模块：含 `setup.sh`、`dolrecomp`、`module-src` |
| `RingOut-<ver>-armada-aarch64.zip` | `steamdeck-x86_64.zip` | 只运行：预编译 runtime，**没有**编译工具，也没有光盘衍生文件 |

两个包都不含 `game/` 和 `bin/g*_recomp.so`（版权：它们来自你的光盘）。

**运行包用法（armada 等 ARM64 机器）：**

1. 在能编译的 aarch64 Linux（或交叉编译）上解压编译包，执行 `./setup.sh /path/to/your-disc.iso`（不要加 `--deck`）
2. 把生成的 `game/` 和 `bin/g<ID>_recomp.so` 拷进运行包
3. `./RingOut`

模块必须是 **ARM aarch64** ELF。用官方 x86 zip 编出来的 `.so` 在 ARM 上无法 `dlopen`。

---

## 在本机构建 runtime

需要：Docker、`aarch64-linux-gnu-gcc` 交叉工具链所用的 Debian 12 arm64 **sysroot**（glibc 2.36）。x86_64 主机走交叉编译，不要用 qemu-user 编 1500 个 Dolphin TU。

```sh
# 1. 准备 Debian 12 arm64 sysroot 到 arm64/sysroot（usr/include + usr/lib/aarch64-linux-gnu）
# 2. 交叉编译 moderngekko-run + dolrecomp
./arm64/build-linux-aarch64.sh

# 3. 按官方 allowlist 打两个 zip（不含 game/ 与模块）
./arm64/package-official-aarch64.sh
```

产物在 `release/`：

- `RingOut-<ver>-linux-aarch64.zip`
- `RingOut-<ver>-armada-aarch64.zip`

在 **aarch64 本机**（Fedora / Debian / Ubuntu）上也可以不交叉，直接用系统 gcc 编；`setup.sh` 仍要 `cmake ninja clang python3`。Fedora atomic（rpm-ostree）需要：

```sh
sudo rpm-ostree install cmake ninja-build clang python3
sudo systemctl reboot
```

---

## 性能说明

客机是单线程 PowerPC。运行时已开 Dolphin dual-core（CPU 线程 + GPU 线程），**不会**把重编译模块摊到 8 核。系统总 CPU 30% 而一核 99%，是正常的 CPU-bound 曲线，不是 GPU 瓶颈。

LTO / 本机 `--pgo` 只加快那一条客机线程（PGO 官方约 10–14%）。交叉编的无 LTO 模块可以玩，但比官方 x86 发货模块慢。aarch64 上不要使用随包的 x86 `.profdata`。

---

## 许可与免责

与上游相同：**GPL-2.0-or-later**，见 [`LICENSE`](LICENSE)。非官方同人项目，与 Bandai Namco、Dolphin 等无关。不要索要光盘或模块。
