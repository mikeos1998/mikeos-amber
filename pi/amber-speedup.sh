#!/bin/bash
# Mike OS: Amber - speed-up for Raspberry Pi 3 / 4.
# Run on the Pi:   sudo bash amber-speedup.sh      then reboot.
# Safe to run more than once.
#
# What it does:
#   Boot:    stops services that slow down start-up and aren't needed on a desktop Pi
#            (wait-for-network, modem manager, background update checks, etc.)
#   Memory:  swap goes to compressed RAM first (much faster than the SD card)
#   Desktop: turns off window effects/animations (the Pi 3's graphics struggle with them)
#            and the software-update checker that runs at every login
#   Firefox: one process instead of many, fewer background features (saves a lot of RAM)

if [ "$(id -u)" != 0 ]; then echo "Run it with sudo:  sudo bash $0"; exit 1; fi
BUILD_TIME=""; [ "${1:-}" = "--build-time" ] && BUILD_TIME=1
TARGET_USER=${SUDO_USER:-amber}; [ "$TARGET_USER" = root ] && TARGET_USER=amber
say() { echo " * $*"; }

echo "=== Mike OS: Amber speed-up ==="

# ---------------------------------------------------------------- boot
say "Boot: turning off services a desktop Pi doesn't need"
for unit in NetworkManager-wait-online.service ModemManager.service triggerhappy.service \
            avahi-daemon.service avahi-daemon.socket cups-browsed.service \
            apt-daily.timer apt-daily-upgrade.timer man-db.timer e2scrub_all.timer \
            packagekit-offline-update.service; do
    if systemctl list-unit-files "$unit" 2>/dev/null | grep -q "$unit"; then
        systemctl disable "$unit" >/dev/null 2>&1 && echo "     - $unit off"
        [ -z "$BUILD_TIME" ] && systemctl stop "$unit" >/dev/null 2>&1
    fi
done
# boot messages / logs stay in RAM instead of being written to the SD card
mkdir -p /etc/systemd/journald.conf.d
printf '[Journal]\nStorage=volatile\nRuntimeMaxUse=32M\n' > /etc/systemd/journald.conf.d/mikeos-amber.conf

# ---------------------------------------------------------------- Wi-Fi
# Raspberry Pi OS keeps Wi-Fi switched off until a country is set.
say "Wi-Fi: setting the country to US and switching Wi-Fi on"
if command -v raspi-config >/dev/null 2>&1; then
    raspi-config nonint do_wifi_country US >/dev/null 2>&1 && echo "     - Wi-Fi country: US"
fi
for f in /var/lib/systemd/rfkill/*:wlan; do [ -e "$f" ] && echo 0 > "$f"; done
[ -z "$BUILD_TIME" ] && command -v rfkill >/dev/null 2>&1 && rfkill unblock wifi
[ -z "$BUILD_TIME" ] && command -v nmcli >/dev/null 2>&1 && nmcli radio wifi on 2>/dev/null

# ---------------------------------------------------------------- memory
say "Memory: compressed RAM swap first, SD-card swap only as a last resort"
if [ -f /etc/default/zramswap ]; then
    printf 'ALGO=zstd\nPERCENT=60\nPRIORITY=100\n' > /etc/default/zramswap
    systemctl enable zramswap.service >/dev/null 2>&1
fi
mkdir -p /etc/sysctl.d
printf 'vm.swappiness=100\nvm.page-cluster=0\nvm.vfs_cache_pressure=200\n' > /etc/sysctl.d/90-mikeos-amber.conf
[ -z "$BUILD_TIME" ] && sysctl -q -p /etc/sysctl.d/90-mikeos-amber.conf

# ---------------------------------------------------------------- desktop
say "Desktop: no window effects or animations, no update checker at login"
desktop_settings() {   # $1 = a home folder
    local h=$1 c
    c="$h/.config"
    mkdir -p "$c/autostart"
    if command -v kwriteconfig5 >/dev/null 2>&1; then
        kwriteconfig5 --file "$c/kwinrc"      --group Compositing --key Enabled false
        kwriteconfig5 --file "$c/kdeglobals"  --group KDE --key AnimationDurationFactor 0
        kwriteconfig5 --file "$c/baloofilerc" --group "Basic Settings" --key Indexing-Enabled false
        kwriteconfig5 --file "$c/ksmserverrc" --group General --key loginMode emptySession
    fi
    # hide heavy background apps from starting at login
    for a in org.kde.discover.notifier org.kde.kdeconnect.daemon geoclue-demo-agent; do
        printf '[Desktop Entry]\nHidden=true\n' > "$c/autostart/$a.desktop"
    done
}
desktop_settings /etc/skel
if [ -n "$BUILD_TIME" ]; then
    :   # users are created from /etc/skel later
elif id "$TARGET_USER" >/dev/null 2>&1; then
    home=$(getent passwd "$TARGET_USER" | cut -d: -f6)
    desktop_settings "$home"
    chown -R "$TARGET_USER:$TARGET_USER" "$home/.config"
    echo "     - applied to user $TARGET_USER"
fi

# ---------------------------------------------------------------- Firefox
say "Firefox: lighter settings"
FF=/usr/lib/firefox-esr
if [ -d "$FF" ]; then
    mkdir -p "$FF/defaults/pref"
    if ! grep -qs 'general.config.filename' "$FF/defaults/pref/autoconfig.js"; then
        printf 'pref("general.config.filename", "mikeos-amber.cfg");\npref("general.config.obscure_value", 0);\npref("general.config.sandbox_enabled", false);\n' \
            > "$FF/defaults/pref/autoconfig.js"
    fi
    CFG="$FF/mikeos-amber.cfg"
    [ -f "$CFG" ] || echo "// Mike OS: Amber Firefox settings (first line must stay a comment)" > "$CFG"
    sed -i '/^\/\/ >>> speed-up/,/^\/\/ <<< speed-up/d' "$CFG"
    cat >> "$CFG" <<'EOF'
// >>> speed-up (added by amber-speedup.sh)
defaultPref("fission.autostart", false);
defaultPref("dom.ipc.processCount", 1);
defaultPref("dom.ipc.processCount.webIsolated", 1);
defaultPref("dom.ipc.processPrelaunch.enabled", false);
defaultPref("browser.sessionstore.interval", 60000);
defaultPref("browser.sessionhistory.max_total_viewers", 0);
defaultPref("network.prefetch-next", false);
defaultPref("network.dns.disablePrefetch", true);
defaultPref("extensions.pocket.enabled", false);
defaultPref("browser.ml.enable", false);
defaultPref("browser.ml.chat.enabled", false);
defaultPref("toolkit.telemetry.enabled", false);
defaultPref("datareporting.healthreport.uploadEnabled", false);
defaultPref("app.shield.optoutstudies.enabled", false);
defaultPref("browser.discovery.enabled", false);
defaultPref("general.smoothScroll", false);
defaultPref("ui.prefersReducedMotion", 1);
// <<< speed-up
EOF
fi

echo "=== Done. ==="
if [ -z "$BUILD_TIME" ]; then
    echo "Restart the Pi to finish:  sudo reboot"
    echo "(The first Firefox start after this is still slow while it sets up; later starts are faster.)"
fi
exit 0
