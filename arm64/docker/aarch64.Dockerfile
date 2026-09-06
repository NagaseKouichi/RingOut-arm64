# Debian 12 (glibc 2.36) aarch64 toolchain for Ring Out.
# Same floor as the upstream Steam Deck job, but the target ISA is ARM64.
FROM debian:12
ENV DEBIAN_FRONTEND=noninteractive
COPY .github/scripts/deck-deps.txt /tmp/deck-deps.txt
RUN apt-get update \
 && apt-get install -y --no-install-recommends \
      $(grep -v '^#' /tmp/deck-deps.txt | tr -s '[:space:]' '\n' | grep -v '^$') \
      zlib1g-dev libvulkan-dev file binutils \
 && rm -rf /var/lib/apt/lists/*
WORKDIR /src
