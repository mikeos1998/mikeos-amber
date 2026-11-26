#!/bin/bash
# Mike OS: Amber - faster internet and web browsing.
#   * Wi-Fi power saving off (it adds lag to every page on a Pi)
#   * a local address cache, so each website's address is looked up once, not every time
#   * built-in ad and tracker blocking for every browser (heavy pages load much faster)
#   * Chromium's low-memory mode, and the Amber search page for Raspberry Pi's Chromium
#
#   sudo bash amber-net-boost.sh          turn it on (or refresh the block list)
#   sudo bash amber-net-boost.sh --undo   turn it all off again
#
# A site that's blocked by mistake can be allowed: add its name (one per line) to
# /etc/mikeos-amber/allow.txt and run this script again.

if [ "$(id -u)" != 0 ]; then echo "Run it with sudo:  sudo bash $0"; exit 1; fi
say() { echo " * $*"; }

NM_CONF=/etc/NetworkManager/conf.d/90-mikeos-amber.conf
DNS_CONF=/etc/NetworkManager/dnsmasq.d/mikeos-amber.conf
DIR=/etc/mikeos-amber
LIST=$DIR/blocklist.hosts
ALLOW=$DIR/allow.txt
LIST_URL=https://raw.githubusercontent.com/StevenBlack/hosts/master/hosts
FF_CFG=/usr/lib/firefox-esr/mikeos-amber.cfg

restart_network() {
    systemctl restart NetworkManager
    for _ in $(seq 1 30); do                 # wait for Wi-Fi to come back
        getent hosts www.google.com >/dev/null 2>&1 && return 0
        sleep 2
    done
    return 1
}

if [ "${1:-}" = "--undo" ]; then
    echo "=== Turning the internet boost off ==="
    rm -f "$NM_CONF" "$DNS_CONF" /etc/chromium.d/91-mikeos-amber-net \
          /etc/chromium/policies/managed/mikeos-amber-dns.json \
          /etc/chromium-browser/policies/managed/mikeos-amber-dns.json
    [ -f "$FF_CFG" ] && sed -i '/^\/\/ >>> net-boost/,/^\/\/ <<< net-boost/d' "$FF_CFG"
    say "Restarting the network..."
    restart_network && say "Back to normal." || say "The network is restarting - give it a minute."
    exit 0
fi

echo "=== Mike OS: Amber - internet boost ==="
if ! curl -s -o /dev/null --max-time 10 https://www.google.com; then
    echo "The Pi needs to be online for this. Connect with the Wi-Fi app, then run this again."
    exit 1
fi

# ------------------------------------------------ the address cache needs dnsmasq
if ! command -v dnsmasq >/dev/null 2>&1; then
    say "Installing the address cache (dnsmasq)..."
    DEBIAN_FRONTEND=noninteractive apt-get install -y -q dnsmasq-base || { echo "!! Couldn't install it - nothing was changed."; exit 1; }
fi

# ------------------------------------------------ ad/tracker block list
say "Downloading the ad and tracker block list..."
mkdir -p "$DIR"
[ -f "$ALLOW" ] || printf '# Sites to never block - one name per line, for example:\n# ads.example.com\n' > "$ALLOW"
tmp=$(mktemp)
if curl -fsSL --max-time 120 "$LIST_URL" -o "$tmp" && [ "$(grep -c '^0\.0\.0\.0 ' "$tmp")" -gt 10000 ]; then
    grep -v '^\s*#' "$ALLOW" | awk 'NF{print tolower($1)}' > "$tmp.allow"
    awk 'FILENAME==ARGV[1]{ok[$1]=1; next} $1=="0.0.0.0" && $2!="0.0.0.0" && !($2 in ok){print "0.0.0.0 " $2}' \
        "$tmp.allow" "$tmp" > "$LIST"
    chmod 644 "$LIST"
    say "Blocking $(wc -l < "$LIST") ad and tracker addresses."
else
    [ -f "$LIST" ] && say "Couldn't download a new list - keeping the one you have." \
                   || { echo "!! Couldn't download the block list - nothing was changed."; rm -f "$tmp"; exit 1; }
fi
rm -f "$tmp" "$tmp.allow"

# ------------------------------------------------ network settings
say "Turning off Wi-Fi power saving and adding the address cache"
mkdir -p "$(dirname "$NM_CONF")" "$(dirname "$DNS_CONF")"
printf '[main]\ndns=dnsmasq\n\n[connection]\nwifi.powersave=2\n' > "$NM_CONF"
printf 'addn-hosts=%s\ncache-size=10000\n' "$LIST" > "$DNS_CONF"

# browsers must use the cache (not their own "secure DNS"), or blocking is skipped
for d in /etc/chromium/policies/managed /etc/chromium-browser/policies/managed; do
    mkdir -p "$d"
    printf '{\n  "DnsOverHttpsMode": "off"\n}\n' > "$d/mikeos-amber-dns.json"
done
# Raspberry Pi's Chromium reads its settings from a different folder: give it the Amber search page too
[ -f /etc/chromium/policies/managed/mikeos-amber.json ] && \
    cp /etc/chromium/policies/managed/mikeos-amber.json /etc/chromium-browser/policies/managed/mikeos-amber.json
mkdir -p /etc/chromium.d
printf '# Mike OS: Amber - Chromium low-memory mode for 1 GB computers\nexport CHROMIUM_FLAGS="$CHROMIUM_FLAGS --enable-low-end-device-mode"\n' \
    > /etc/chromium.d/91-mikeos-amber-net
if [ -f "$FF_CFG" ]; then
    sed -i '/^\/\/ >>> net-boost/,/^\/\/ <<< net-boost/d' "$FF_CFG"
    printf '// >>> net-boost (added by amber-net-boost.sh)\ndefaultPref("network.trr.mode", 5);\n// <<< net-boost\n' >> "$FF_CFG"
fi

# ------------------------------------------------ apply, and undo automatically if it breaks
say "Restarting the network (Wi-Fi drops for a few seconds)..."
if ! restart_network; then
    say "Websites didn't load with the new settings - putting the old ones back."
    rm -f "$NM_CONF" "$DNS_CONF"
    restart_network
    echo "!! The boost was undone so your internet keeps working. Tell Claude what happened."
    exit 1
fi
if getent hosts doubleclick.net 2>/dev/null | grep -q '^0\.0\.0\.0'; then
    say "Ad blocking is working."
else
    say "Internet works; ad blocking will kick in after a restart."
fi
echo "=== Done. Close and reopen the browser to feel the difference. ==="
echo "To turn it all off:  sudo bash $0 --undo"
