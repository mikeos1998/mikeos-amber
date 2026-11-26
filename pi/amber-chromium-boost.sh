#!/bin/bash
# Mike OS: Amber - make Chromium lighter and faster on the Raspberry Pi.
#   * uBlock Origin Lite installed automatically (blocks ads and trackers inside pages)
#   * Memory Saver on its strongest setting (tabs you're not looking at are unloaded)
#   * no page pre-loading, translate or spell check running in the background
#   * 2 page processes instead of 3, and smooth scrolling off
#
#   sudo bash amber-chromium-boost.sh          turn it on
#   sudo bash amber-chromium-boost.sh --undo   put it back how it was
if [ "$(id -u)" != 0 ]; then echo "Run it with sudo:  sudo bash $0"; exit 1; fi
POL=mikeos-amber-speed.json
FLAGS=/etc/chromium.d/92-mikeos-amber-speed

if [ "${1:-}" = "--undo" ]; then
    rm -f /etc/chromium/policies/managed/$POL /etc/chromium-browser/policies/managed/$POL "$FLAGS"
    echo "Chromium is back to how it was. Close and reopen it."
    echo "(uBlock Origin Lite stays until you remove it from Chromium's Extensions page.)"
    exit 0
fi

for d in /etc/chromium/policies/managed /etc/chromium-browser/policies/managed; do
    mkdir -p "$d"
    cat > "$d/$POL" <<'POLICY'
{
  "ExtensionInstallForcelist": ["ddkjiahejlhfcafbddmgiahcphecmpfh;https://clients2.google.com/service/update2/crx"],
  "MemorySaverModeSavings": 2,
  "HighEfficiencyModeEnabled": true,
  "NetworkPredictionOptions": 2,
  "TranslateEnabled": false,
  "SpellcheckEnabled": false,
  "SyncDisabled": true,
  "BrowserSignin": 0
}
POLICY
    chmod 644 "$d/$POL"
done
mkdir -p /etc/chromium.d
printf '# Mike OS: Amber - lighter Chromium for the Pi\nexport CHROMIUM_FLAGS="$CHROMIUM_FLAGS --renderer-process-limit=2 --disable-smooth-scrolling"\n' > "$FLAGS"
chmod 644 "$FLAGS"
echo "Done! Close Chromium completely and open it again."
echo "uBlock Origin Lite installs itself within a minute the first time (needs the internet)."
echo "To undo:  sudo bash $0 --undo"
