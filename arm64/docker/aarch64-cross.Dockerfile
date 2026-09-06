# Native-speed aarch64 cross toolchain (runs on amd64).
# Compiles with aarch64-linux-gnu-g++ against a Debian 12 arm64 sysroot.
FROM ubuntu:24.04
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update \
 && apt-get install -y --no-install-recommends \
      g++-aarch64-linux-gnu gcc-aarch64-linux-gnu \
      cmake ninja-build pkg-config python3 git zip file \
      wayland-protocols libwayland-bin \
      ca-certificates ccache \
 && rm -rf /var/lib/apt/lists/*
WORKDIR /src
