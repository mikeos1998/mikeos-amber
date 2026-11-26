#!/bin/bash
# Mike OS: Amber - replace Chromium with Epiphany (GNOME Web), the lighter browser.
#   sudo bash replace-chromium-with-epiphany.sh
# (or: Amber Store -> Install from file...)
# Installs Epiphany first, makes it the browser everything opens (desktop icon, panel, links),
# and only then removes Chromium. Firefox, if you have it, is left alone.
set -e
if [ "$(id -u)" != 0 ]; then echo "Run it with sudo:  sudo bash $0"; exit 1; fi
export DEBIAN_FRONTEND=noninteractive
say() { echo "STATUS:$*"; }

say "Installing Epiphany (GNOME Web)..."
PKGS="epiphany-browser gstreamer1.0-plugins-base gstreamer1.0-plugins-good gstreamer1.0-libav dconf-gsettings-backend ca-certificates"
apt-get install -y -q --no-install-recommends $PKGS || { apt-get update -q && apt-get install -y -q --no-install-recommends $PKGS; }
[ -x /usr/bin/epiphany-browser ] || { echo "ERROR: Epiphany didn't install - Chromium was left as it is."; exit 1; }

say "Setting up Epiphany..."
mkdir -p /usr/share/mikeos-amber/bin /usr/share/glib-2.0/schemas /usr/local/share/applications
cat > /usr/share/glib-2.0/schemas/90_mikeos-amber-epiphany.gschema.override <<'AMBER_FILE_EOF'
# Mike OS: Amber - Epiphany (GNOME Web) defaults
[org.gnome.Epiphany]
homepage-url='file:///usr/share/mikeos-amber/newtab/index.html'
default-search-engine='Google'
restore-session-policy='crashed'
ask-for-default=false
AMBER_FILE_EOF
cat > /usr/share/mikeos-amber/bin/amber-epiphany <<'AMBER_FILE_EOF'
#!/bin/sh
# Epiphany (GNOME Web) for Mike OS: Amber.
# On a Raspberry Pi 3 or older, only use the graphics chip when a page really needs it (saves memory);
# set once per user, so it can still be changed in Preferences.
model=$(tr -d '\0' < /proc/device-tree/model 2>/dev/null)
mark="${XDG_CONFIG_HOME:-$HOME/.config}/mikeos-amber/epiphany-tuned"
case "$model" in
    *"Raspberry Pi 3"*|*"Raspberry Pi 2"*|*"Raspberry Pi Model"*|*"Raspberry Pi Zero"*)
        if [ ! -e "$mark" ]; then
            gsettings set org.gnome.Epiphany.web:/org/gnome/epiphany/web/ hardware-acceleration-policy on-demand 2>/dev/null
            mkdir -p "$(dirname "$mark")" && touch "$mark"
        fi ;;
esac
export WEBKIT_DISABLE_DMABUF_RENDERER=1      # avoids blank pages with some graphics drivers
exec epiphany-browser "$@"
AMBER_FILE_EOF
cat > /usr/share/mikeos-amber/bin/amber-browser <<'AMBER_FILE_EOF'
#!/bin/sh
# Opens the web browser: Epiphany (GNOME Web) - the lightest one - with Chromium or Firefox as spares.
[ -x /usr/bin/epiphany-browser ] && exec /usr/share/mikeos-amber/bin/amber-epiphany "$@"
for app in chromium-browser chromium firefox-esr; do
    command -v "$app" >/dev/null 2>&1 && exec "$app" "$@"
done
exec xdg-open "${1:-https://www.google.com}"
AMBER_FILE_EOF
cat > /usr/local/share/applications/org.gnome.Epiphany.desktop <<'AMBER_FILE_EOF'
[Desktop Entry]
Type=Application
Name=Web Browser
GenericName=Web Browser
Comment=Browse the web (GNOME Web)
Exec=/usr/share/mikeos-amber/bin/amber-epiphany %U
Icon=org.gnome.Epiphany
Terminal=false
StartupNotify=true
StartupWMClass=epiphany
Categories=Network;WebBrowser;
MimeType=text/html;application/xhtml+xml;x-scheme-handler/http;x-scheme-handler/https;
Actions=new-window;

[Desktop Action new-window]
Name=New Window
Exec=/usr/share/mikeos-amber/bin/amber-epiphany --new-window
AMBER_FILE_EOF
chmod 644 /usr/share/glib-2.0/schemas/90_mikeos-amber-epiphany.gschema.override /usr/local/share/applications/org.gnome.Epiphany.desktop
chmod 755 /usr/share/mikeos-amber/bin/amber-epiphany /usr/share/mikeos-amber/bin/amber-browser
glib-compile-schemas /usr/share/glib-2.0/schemas
rm -f /usr/local/bin/amber-epiphany                 # from the store's Epiphany app, now built in

say "Making Epiphany the default browser..."
update-alternatives --set x-www-browser /usr/bin/epiphany-browser >/dev/null 2>&1 || true
# links and web pages open with the Amber browser launcher (= Epiphany) for everyone
for f in /etc/xdg/mimeapps.list /home/*/.config/mimeapps.list /home/*/.config/lxqt-mimeapps.list \
         /usr/share/mikeos-amber/lite/lxqt-mimeapps.list /etc/skel/.config/lxqt-mimeapps.list; do
    [ -f "$f" ] || continue
    sed -i -E 's#^((text/html|x-scheme-handler/https?)=).*#\1mikeos-amber-browser.desktop#' "$f"
done
command -v update-desktop-database >/dev/null && update-desktop-database -q /usr/local/share/applications || true

say "Removing Chromium..."
gone=$(dpkg-query -W -f='${Package} ${db:Status-Status}\n' 'chromium*' 'rpi-chromium-mods' 2>/dev/null | awk '$2=="installed"{print $1}')
[ -n "$gone" ] && apt-get purge -y -q $gone
apt-get autoremove -y -q || true
rm -f /etc/chromium.d/9*-mikeos-amber* /etc/chromium/policies/managed/mikeos-amber*.json \
      /etc/chromium-browser/policies/managed/mikeos-amber*.json

echo "Done! Chromium is gone and the Web Browser icon now opens Epiphany (GNOME Web)."
