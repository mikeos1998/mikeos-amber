#!/bin/bash
# Mike OS: Amber - Pi checkup. Finds out WHY the Pi is slow.
# Run on the Pi:   sudo bash /boot/firmware/amber-checkup.sh
# Writes a report to the SD card's boot drive (amber-report.txt), which you can open on Windows.

if [ "$(id -u)" != 0 ]; then echo "Run it with sudo:  sudo bash $0"; exit 1; fi
OUT=/boot/firmware/amber-report.txt
[ -d /boot/firmware ] || OUT=/root/amber-report.txt
VERDICT=()

exec > >(tee "$OUT") 2>&1
echo "=== Mike OS: Amber checkup  $(date) ==="

echo; echo "--- Power and temperature"
if command -v vcgencmd >/dev/null 2>&1; then
    t=$(vcgencmd get_throttled | cut -d= -f2)
    echo "throttled=$t   $(vcgencmd measure_temp)   CPU $(( $(vcgencmd measure_clock arm | cut -d= -f2) / 1000000 )) MHz"
    v=$((t))
    (( v & 0x1 ))     && VERDICT+=("POWER: under-voltage RIGHT NOW. The power supply is too weak, so the Pi slows itself down. Use a 5V 2.5A Raspberry Pi power supply.")
    (( v & 0x10000 )) && ! (( v & 0x1 )) && VERDICT+=("POWER: under-voltage happened since boot. The power supply or cable is too weak. Use a 5V 2.5A Raspberry Pi power supply.")
    (( v & 0x4 ))     && VERDICT+=("HEAT/POWER: the Pi is throttling its speed right now.")
    (( v & 0x8 ))     && VERDICT+=("HEAT: the Pi is too hot. Give it airflow or a heatsink.")
else
    echo "(vcgencmd not found)"
fi
dmesg 2>/dev/null | grep -i -m3 "under-voltage" && VERDICT+=("POWER: the system log shows under-voltage warnings.")

echo; echo "--- SD card speed"
dev=$(findmnt -n -o SOURCE /); echo "system is on $dev"
dd if=/dev/zero of=/var/tmp/amber-speedtest bs=1M count=64 oflag=direct conv=fsync 2>&1 | tail -1 | sed 's/^/big write:   /'
w=$(dd if=/dev/zero of=/var/tmp/amber-speedtest bs=1M count=64 oflag=direct conv=fsync 2>&1 | tail -1 | grep -o '[0-9.,]* [MkG]B/s')
echo 3 > /proc/sys/vm/drop_caches
dd if=/var/tmp/amber-speedtest of=/dev/null bs=1M iflag=direct 2>&1 | tail -1 | sed 's/^/big read:    /'
start=$(date +%s.%N)
dd if=/dev/zero of=/var/tmp/amber-speedtest bs=4k count=200 oflag=dsync 2>/dev/null
small=$(echo "$(date +%s.%N) - $start" | bc 2>/dev/null || python3 -c "print(round($(date +%s.%N)-$start,2))")
echo "200 small writes took ${small}s  (a decent card: under 2s)"
rm -f /var/tmp/amber-speedtest
case "$w" in *kB/s) VERDICT+=("SD CARD: writing is extremely slow ($w). Replace the card with a name-brand A1/A2 card.") ;; esac
awk -v s="$small" 'BEGIN{exit !(s>6)}' && VERDICT+=("SD CARD: small writes are very slow (${small}s). This card makes every app crawl. Use a name-brand A1/A2 card (SanDisk, Samsung).")
dmesg 2>/dev/null | grep -i -E -m5 "mmc.*(error|timeout)|I/O error|EXT4-fs error" && VERDICT+=("SD CARD: the system log shows read/write errors - the card or its contacts are failing.")

echo; echo "--- Memory"
free -h
swapon --show
used_swap=$(free -m | awk '/Swap/{print $3}')
[ "${used_swap:-0}" -gt 400 ] && VERDICT+=("MEMORY: ${used_swap} MB of swap in use - too many things running for 1 GB of RAM.")
swapon --show | grep -q zram || VERDICT+=("MEMORY: compressed RAM swap (zram) is not active. Run amber-speedup.sh.")

echo; echo "--- What's using the most memory / CPU"
ps -eo pid,user,rss,pcpu,comm --sort=-rss | head -12 | awk 'NR==1{print; next}{printf "%-7s %-10s %6d MB %5s%% %s\n",$1,$2,$3/1024,$4,$5}'
echo "load average: $(cut -d' ' -f1-3 /proc/loadavg)"

echo; echo "--- Graphics"
log=$(ls /var/log/Xorg.0.log /home/*/.local/share/xorg/Xorg.0.log 2>/dev/null | head -1)
if [ -n "$log" ]; then
    grep -E -m4 "glamor|llvmpipe|modeset\(0\): .*driver|AIGLX: Loaded" "$log"
    grep -q "llvmpipe" "$log" && VERDICT+=("GRAPHICS: the desktop is drawing with the CPU instead of the graphics chip.")
else
    echo "(no X log found)"
fi
lsmod | grep -q "^vc4" && echo "vc4 graphics driver: loaded" || VERDICT+=("GRAPHICS: the Pi graphics driver (vc4) is not loaded.")
grep -E "^(dtoverlay=vc4|arm_|over_voltage|gpu_mem)" /boot/firmware/config.txt 2>/dev/null

echo; echo "--- Disk space and boot"
df -h / | tail -1
systemd-analyze 2>/dev/null | head -1
systemd-analyze blame 2>/dev/null | head -10

echo; echo "================ RESULT ================"
if [ ${#VERDICT[@]} -eq 0 ]; then
    echo "No obvious hardware problem found. Send this report to Claude."
else
    for x in "${VERDICT[@]}"; do echo "!! $x"; done
fi
echo "Report saved to $OUT"
