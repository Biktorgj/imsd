#!/bin/sh
# Small script I use to check the build in real hardware
cd /root/imsd && git pull && rm -rf build && meson setup build && ninja -C build && ./build/imsd --device=qrtr://0
