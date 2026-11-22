# M.I.K.E for Linux (Debian/Ubuntu)
#   sudo apt install build-essential libsdl2-dev libgl-dev
#   make            -> ./mike
#   ./mike          (or ./mike --fullscreen)
#   sudo make install / sudo make uninstall   (optional, installs to /usr/local)
# Dreamcast build: see Makefile.dc

CC      ?= gcc
PREFIX  ?= /usr/local
CFLAGS  ?= -O2
CFLAGS  += -Wall -Wno-unused-function -Isrc $(shell sdl2-config --cflags) -DDATADIR='"$(PREFIX)/share/mike"'
LDLIBS  += $(shell sdl2-config --libs) -lGL -lm

OBJS = build/linux/game.o build/linux/level.o build/linux/plat_pc.o build/linux/vmu_emu.o

all: mike

mike: $(OBJS)
	$(CC) -o $@ $(OBJS) $(LDFLAGS) $(LDLIBS)

build/linux/%.o: src/%.c src/platform.h src/level.h src/assets.h
	@mkdir -p build/linux
	$(CC) $(CFLAGS) -c -o $@ $<

build/linux/game.o: src/race.inc src/editor.inc src/pet.inc src/pet_vms.h src/vmu_emu.h
build/linux/plat_pc.o: src/pet_vms.h src/vmu_emu.h src/sfx_data.h src/saveedit_font.h

# the VMU pet: src/pet_vms.h is generated from src/vmupet/*.s (needs python3); it is also kept in the source
pet:
	python3 tools/mkpet.py

install: mike
	install -Dm755 mike $(DESTDIR)$(PREFIX)/games/mike
	install -Dm644 data/TITLE.WAV $(DESTDIR)$(PREFIX)/share/mike/TITLE.WAV
	install -Dm644 data/JUMP.WAV $(DESTDIR)$(PREFIX)/share/mike/JUMP.WAV
	install -Dm644 data/CREDITS.WAV $(DESTDIR)$(PREFIX)/share/mike/CREDITS.WAV
	install -Dm644 linux/mike.desktop $(DESTDIR)$(PREFIX)/share/applications/mike.desktop
	install -Dm644 linux/mike.png $(DESTDIR)$(PREFIX)/share/icons/hicolor/256x256/apps/mike.png

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/games/mike $(DESTDIR)$(PREFIX)/share/applications/mike.desktop \
	      $(DESTDIR)$(PREFIX)/share/icons/hicolor/256x256/apps/mike.png
	rm -rf $(DESTDIR)$(PREFIX)/share/mike

clean:
	rm -f mike $(OBJS)

.PHONY: all install uninstall clean
