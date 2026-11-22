M.I.K.E - Linux source (Debian / Ubuntu)
(C) 2026 Prosystems Corp

1. Install the build tools:
     sudo apt install build-essential libsdl2-dev libgl-dev dpkg-dev

2a. Make a Debian package and install it (recommended):
     sh linux/build-deb.sh
     sudo apt install ./mike_1.0_amd64.deb
   M.I.K.E then shows up in your apps menu under Games, or run "mike" in a terminal.
   Remove it with: sudo apt remove mike

2b. Or just build and play from this folder:
     make
     ./mike            (./mike --fullscreen for fullscreen, F11 toggles)
   Or install to /usr/local instead:  sudo make install   (sudo make uninstall to remove)

Saves go in ~/.local/share/Prosystems/MIKE/.

src/     game source (game.c, level.c, plat_pc.c, vmu_emu.c and headers)
data/    music (TITLE, CREDITS, JUMP .WAV)
linux/   app menu entry, icon, and build-deb.sh
tools/   Python scripts that regenerate the generated headers (not needed to build):
           mkpet.py -> src/pet_vms.h (VMU pet, from src/vmupet/*.s)
           mksfx.py -> src/sfx_data.h (from assets/sfx)
           mkeditfont.py -> src/saveedit_font.h
