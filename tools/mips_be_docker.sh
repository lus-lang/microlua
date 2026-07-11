#!/bin/sh
# Run the big-endian MIPS test suite (cross/mips-be-qemu.ini) inside Docker,
# for hosts without the Debian cross toolchain (macOS, post-buster Linux).
# Debian buster is the last release with the big-endian 32-bit MIPS cross gcc.
set -e

REPO=$(cd "$(dirname "$0")/.." && pwd)
IMG=mlua-mips-be

docker image inspect $IMG >/dev/null 2>&1 || docker build -t $IMG - <<'EOF'
FROM --platform=linux/amd64 debian:buster
RUN printf 'deb http://archive.debian.org/debian buster main\ndeb http://archive.debian.org/debian-security buster/updates main\n' > /etc/apt/sources.list \
 && apt-get -o Acquire::Check-Valid-Until=false update \
 && apt-get install -y --no-install-recommends gcc-mips-linux-gnu \
    libc6-dev-mips-cross qemu-user ninja-build python3-pip \
    python3-setuptools python3-wheel \
 && pip3 install 'meson==0.61.5' \
 && apt-get clean
EOF

exec docker run --rm --platform linux/amd64 -v "$REPO":/mnt/microlua -w /mnt/microlua $IMG \
    sh -c "rm -rf build-mipsbe-docker \
        && meson setup build-mipsbe-docker --cross-file cross/mips-be-qemu.ini >/dev/null \
        && meson test -C build-mipsbe-docker --print-errorlogs"
