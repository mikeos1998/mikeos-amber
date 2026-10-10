#!/bin/bash
# Builds the Mike OS: Amber ISO. Run as root on Debian 12 (GitHub does this for you).
set -euo pipefail
cd "$(dirname "$0")"
export DEBIAN_FRONTEND=noninteractive

apt-get update
apt-get install -y live-build git python3 ca-certificates

# files uploaded through the GitHub website lose their "executable" flag
chmod +x auto/config config/hooks/normal/*.hook.chroot scripts/*.py

python3 scripts/fetch_theme.py

lb clean --purge || true
lb config
lb build 2>&1 | tee build.log

mkdir -p out
mv live-image-amd64.hybrid.iso out/mikeos-amber.iso
ls -lh out/
