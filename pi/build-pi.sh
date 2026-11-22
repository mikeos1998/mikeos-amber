#!/bin/bash
# Builds Mike OS: Amber for Raspberry Pi 3/4 (64-bit) as an SD-card image.
# Run as root on a 64-bit ARM Linux machine (GitHub does this for you):  sudo bash pi/build-pi.sh
# Output: out-pi/mikeos-amber-pi.img.xz  -> write it with Raspberry Pi Imager ("Use custom").
#
# It starts from the official Raspberry Pi OS Lite (Legacy, 64-bit, Debian 12 "bookworm"),
# adds KDE Plasma + everything from config/ (theme, apps, games, sounds), creates the user
# "amber" (password "amber") and logs straight into the desktop.

set -euo pipefail
cd "$(dirname "$0")/.."
REPO=$PWD
: > "$REPO/pi-build.log"
exec > >(tee -a "$REPO/pi-build.log") 2>&1

BASE_URL=${BASE_URL:-https://downloads.raspberrypi.com/raspios_oldstable_lite_arm64/images/raspios_oldstable_lite_arm64-2026-10-06/2026-10-06-raspios-bookworm-arm64-lite.img.xz}
BASE_SHA=${BASE_SHA:-9e938c5a6981c3f498b3b564c527f3e1fd58ef20bbf703be0eea748384a6c1af}
GROW=${GROW:-6G}                      # room for the desktop while building (shrunk again at the end)
WORK=$REPO/.pi-work
OUT=$REPO/out-pi
IMG=$WORK/mikeos-amber-pi.img
ROOT=$WORK/root
USERNAME=amber
PASSWORD=amber

echo "=== Building Mike OS: Amber build $(cat VERSION 2>/dev/null || echo unknown) for Raspberry Pi ==="
[ "$(id -u)" = 0 ] || { echo "Run this as root (sudo)."; exit 1; }
case "$(uname -m)" in aarch64|arm64) ;; *) echo "This must run on a 64-bit ARM machine (it is $(uname -m))."; exit 1 ;; esac

BOOTDEV=""; ROOTDEV=""
cleanup() {
    set +e
    for m in boot/firmware dev/pts dev proc sys run; do
        mountpoint -q "$ROOT/$m" && umount -l "$ROOT/$m"
    done
    mountpoint -q "$ROOT" && umount -l "$ROOT"
    [ -n "$BOOTDEV" ] && losetup -d "$BOOTDEV" 2>/dev/null
    [ -n "$ROOTDEV" ] && losetup -d "$ROOTDEV" 2>/dev/null
}
trap cleanup EXIT

in_root() { chroot "$ROOT" /usr/bin/env -i HOME=/root PATH=/usr/sbin:/usr/bin:/sbin:/bin \
            LANG=C.UTF-8 DEBIAN_FRONTEND=noninteractive "$@"; }

# Attach the image's two partitions as loop devices, by their position in the image
# (works on any machine, no udev needed).
part_dev() {
    local n=$1 start size
    start=$(partx -g -o START -n "$n" "$IMG" | tr -d ' ')
    size=$(partx -g -o SECTORS -n "$n" "$IMG" | tr -d ' ')
    losetup -f --show -o $((start * 512)) --sizelimit $((size * 512)) "$IMG"
}
attach() { BOOTDEV=$(part_dev 1); ROOTDEV=$(part_dev 2); }
detach() { losetup -d "$BOOTDEV" "$ROOTDEV"; BOOTDEV=""; ROOTDEV=""; }

# ------------------------------------------------------------------ 1. base image
echo "--- [1/7] Getting Raspberry Pi OS Lite"
apt-get update -qq
DEBIAN_FRONTEND=noninteractive apt-get install -y -qq parted fdisk e2fsprogs dosfstools xz-utils \
    curl ca-certificates git python3 rsync >/dev/null
rm -rf "$WORK" "$OUT"; mkdir -p "$WORK" "$OUT" "$ROOT"
if [ -n "${BASE_IMAGE:-}" ]; then
    cp "$BASE_IMAGE" "$IMG"                                   # testing: a local image
else
    curl -fL --retry 3 -o "$WORK/base.img.xz" "$BASE_URL"
    echo "$BASE_SHA  $WORK/base.img.xz" | sha256sum -c -
    xz -d -T0 -c "$WORK/base.img.xz" > "$IMG"
    rm "$WORK/base.img.xz"
fi

# ------------------------------------------------------------------ 2. make room
echo "--- [2/7] Making room for the desktop"
truncate -s +"$GROW" "$IMG"
parted -s "$IMG" resizepart 2 100%
attach
e2fsck -fy "$ROOTDEV" >/dev/null || true
resize2fs "$ROOTDEV" >/dev/null
mount "$ROOTDEV" "$ROOT"
mount "$BOOTDEV" "$ROOT/boot/firmware"
df -h "$ROOT" | tail -1

if [ -n "${SKIP_CHROOT:-}" ]; then
    echo "--- (test mode: skipping the software install)"
    echo "hello" > "$ROOT/test-file"
else
# ------------------------------------------------------------------ 3. desktop + apps
echo "--- [3/7] Installing KDE Plasma and the apps"
for m in dev dev/pts proc sys run; do mount --bind "/$m" "$ROOT/$m"; done
cp -L "$ROOT/etc/resolv.conf" "$WORK/resolv.conf.orig" 2>/dev/null || true
rm -f "$ROOT/etc/resolv.conf"
if [ -s /run/systemd/resolve/resolv.conf ]; then cp /run/systemd/resolve/resolv.conf "$ROOT/etc/resolv.conf"
else printf 'nameserver 1.1.1.1\nnameserver 8.8.8.8\n' > "$ROOT/etc/resolv.conf"; fi
printf '#!/bin/sh\nexit 101\n' > "$ROOT/usr/sbin/policy-rc.d"; chmod 755 "$ROOT/usr/sbin/policy-rc.d"

in_root apt-get update
# shellcheck disable=SC2046
in_root apt-get install -y -o APT::Install-Recommends=true $(grep -v '^\s*#' pi/packages.list | grep -v '^\s*$')

# ------------------------------------------------------------------ 4. Amber look, apps, games
echo "--- [4/7] Adding the Amber theme, apps and games"
python3 scripts/fetch_theme.py
rsync -a --exclude 'etc/default/grub.d' --exclude 'usr/share/mikeos-amber/installer' \
    config/includes.chroot/ "$ROOT/"
sed -i 's/^VERSION=.*/VERSION="1 (Amber) build '"$(cat VERSION)"' for Raspberry Pi"/' "$ROOT/usr/lib/os-release"
mkdir -p "$ROOT/tmp/hooks"
for h in config/hooks/normal/*.hook.chroot; do
    case "$h" in *installer*) continue ;; esac        # PC installer only
    cp "$h" "$ROOT/tmp/hooks/"
done
for h in "$ROOT"/tmp/hooks/*; do
    echo "--- running $(basename "$h")"
    in_root sh "/tmp/hooks/$(basename "$h")"
done
rm -rf "$ROOT/tmp/hooks"

# ------------------------------------------------------------------ 5. Pi settings
echo "--- [5/7] Pi settings: user, auto-login, memory, language"
# memory: compressed RAM swap plus a 512 MB swap file, no file indexing
printf 'ALGO=zstd\nPERCENT=50\nPRIORITY=100\n' > "$ROOT/etc/default/zramswap"
[ -f "$ROOT/etc/dphys-swapfile" ] && sed -i 's/^#\{0,1\}CONF_SWAPSIZE=.*/CONF_SWAPSIZE=512/' "$ROOT/etc/dphys-swapfile"
mkdir -p "$ROOT/etc/skel/.config"
printf '[Basic Settings]\nIndexing-Enabled=false\n' > "$ROOT/etc/skel/.config/baloofilerc"

# language, keyboard, time zone (US English, US keyboard, Mountain time)
sed -i 's/^# *en_US.UTF-8 UTF-8/en_US.UTF-8 UTF-8/' "$ROOT/etc/locale.gen"
in_root locale-gen
in_root update-locale LANG=en_US.UTF-8
sed -i 's/^XKBLAYOUT=.*/XKBLAYOUT="us"/' "$ROOT/etc/default/keyboard"
ln -sf /usr/share/zoneinfo/America/Denver "$ROOT/etc/localtime"
echo "America/Denver" > "$ROOT/etc/timezone"

# computer name
echo "mikeos-amber" > "$ROOT/etc/hostname"
sed -i 's/^127\.0\.1\.1.*/127.0.1.1\tmikeos-amber/' "$ROOT/etc/hosts"

# the user (made after the theme so it gets the Amber desktop), and auto-login to Plasma
groups=""
for g in sudo audio video render input netdev plugdev users gpio i2c spi bluetooth lpadmin; do
    in_root getent group "$g" >/dev/null && groups="$groups${groups:+,}$g"
done
in_root useradd -m -s /bin/bash -c "Amber" -G "$groups" "$USERNAME"
echo "$USERNAME:$PASSWORD" | in_root chpasswd
mkdir -p "$ROOT/etc/sddm.conf.d"
printf '[Autologin]\nUser=%s\nSession=plasma\n' "$USERNAME" > "$ROOT/etc/sddm.conf.d/10-mikeos-autologin.conf"
in_root systemctl set-default graphical.target
in_root systemctl enable sddm
in_root systemctl mask userconfig.service >/dev/null 2>&1 || true   # skip the "create a user" console wizard

# quiet boot behind the boot screen
CMD="$ROOT/boot/firmware/cmdline.txt"
for opt in quiet splash plymouth.ignore-serial-consoles loglevel=3 vt.global_cursor_default=0; do
    grep -qw -- "$opt" "$CMD" || sed -i "1 s/\$/ $opt/" "$CMD"
done
echo "cmdline.txt: $(cat "$CMD")"

# tidy up
in_root apt-get clean
rm -f "$ROOT/usr/sbin/policy-rc.d" "$ROOT/etc/resolv.conf"
[ -f "$WORK/resolv.conf.orig" ] && cp "$WORK/resolv.conf.orig" "$ROOT/etc/resolv.conf"
rm -rf "$ROOT/var/lib/apt/lists/"* "$ROOT/tmp/"*
for m in run sys proc dev/pts dev; do umount -l "$ROOT/$m"; done
fi

# ------------------------------------------------------------------ 6. shrink
echo "--- [6/7] Shrinking the image (it grows to fill the SD card on first boot)"
df -h "$ROOT" | tail -1
umount "$ROOT/boot/firmware"; umount "$ROOT"
e2fsck -fy "$ROOTDEV" >/dev/null || true
resize2fs -M "$ROOTDEV" >/dev/null 2>&1 || resize2fs -M "$ROOTDEV"
bsize=$(dumpe2fs -h "$ROOTDEV" 2>/dev/null | awk -F: '/^Block size/{gsub(/ /,"",$2); print $2}')
bcount=$(dumpe2fs -h "$ROOTDEV" 2>/dev/null | awk -F: '/^Block count/{gsub(/ /,"",$2); print $2}')
detach
start=$(partx -g -o START -n 2 "$IMG" | tr -d ' ')
sectors=$(( (bcount * bsize + 256 * 1024 * 1024) / 512 ))       # filesystem + 256 MB spare
echo "$start,$sectors" | sfdisk --no-reread -N 2 "$IMG" >/dev/null
truncate -s $(( (start + sectors) * 512 )) "$IMG"
attach; e2fsck -fn "$ROOTDEV" >/dev/null && echo "filesystem check after shrinking: OK"; detach
ls -lh "$IMG"

# ------------------------------------------------------------------ 7. compress
echo "--- [7/7] Compressing"
xz -T0 -6 -c "$IMG" > "$OUT/mikeos-amber-pi.img.xz"
rm -f "$IMG"
ls -lh "$OUT"
echo "=== Done: out-pi/mikeos-amber-pi.img.xz ==="
