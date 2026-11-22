# Mike OS: Amber

A retro, orange-and-purple Linux desktop built on Debian 12 and KDE Plasma, as a bootable live ISO for regular (x86, 64-bit) PCs.

## Build it

1. Open the **Actions** tab of this repository on GitHub.
2. Pick **Build ISO**, then **Run workflow**. It also runs on every change to `main`.
3. When it finishes (about 30 to 60 minutes), open the run and download **mikeos-amber-iso** from the Artifacts section.
4. Unzip it, write `mikeos-amber.iso` to a USB stick with Rufus or balenaEtcher, and boot a PC from the stick.

The live user is `amber`, password `live`. Nothing is written to the PC's drive.

## Where things are

| What | Where |
|---|---|
| Apps to install | `config/package-lists/amber.list.chroot` |
| Build settings (time zone, user name, keyboard) | `auto/config` |
| Colors | `config/includes.chroot/usr/share/color-schemes/Amber.colors` |
| Snake and Welcome Center apps | `config/includes.chroot/usr/share/mikeos-amber/apps/` |
| User picture, installer slides | `config/includes.chroot/usr/share/mikeos-amber/` |
| Boot screen | `config/includes.chroot/usr/share/plymouth/themes/mikeos-amber/` |
| Browser start / new tab page | `config/includes.chroot/usr/share/mikeos-amber/newtab/index.html` |
| Taskbar layout and wallpaper choice | `config/includes.chroot/usr/share/plasma/look-and-feel/org.mikeos.amber/contents/layouts/` |
| Logo | `config/includes.chroot/usr/share/pixmaps/mikeos-amber.svg` |
| Wallpaper | `config/includes.chroot/usr/share/wallpapers/MikeOS-Amber/` |
| 8-bit sounds | `config/includes.chroot/usr/share/sounds/mikeos-amber/` |
| Theme download and recolor | `scripts/fetch_theme.py` |

Anything under `config/includes.chroot/` is copied onto the OS at the same path.

## Credits

The panel theme and window borders are a recolored fork of [Reactionary](https://www.opencode.net/phob1an/reactionary) by phob1an (GPLv3). It is downloaded and recolored during the build.

## Raspberry Pi version (Pi 3 / Pi 4, 64-bit)

Built by the **Build Raspberry Pi image** workflow (its file is `pi/pi-workflow.yml`; on GitHub it lives at `.github/workflows/pi.yml`).
Download **mikeos-amber-pi-image**, unzip it, and write `mikeos-amber-pi.img.xz` to an SD card (8 GB or bigger) with Raspberry Pi Imager:
Choose OS > Use custom. When Imager asks about customising settings, choose **No**.

It logs straight into the desktop. User `amber`, password `amber`. First boot takes a few minutes while it expands to fill the card.
The Pi version leaves out LibreOffice and the hard-drive installer (the SD card is already the installed system).
