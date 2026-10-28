# BQ-3590 apps (friend's v1_os, built on Linux).
# Override machine paths on the command line if needed.
NDK ?= $(HOME)/opt/android-ndk-r25b
NDK_BIN = $(NDK)/toolchains/llvm/prebuilt/linux-x86_64/bin
ARM = TOOLCHAIN=$(NDK_BIN)/llvm CC=$(NDK_BIN)/armv7a-linux-androideabi21-clang NM=$(NDK_BIN)/llvm-nm

FP = fpdoom
SPD = spreadtrum_flash
DATA = phone-data
# python with matplotlib (formulas in books): make venv
PY = .venv/bin/python3
# volume label set by sudo/02_format_sd_fat32.sh
CARD_LABEL ?= BQ3590

.PHONY: all venv tools apps apps-sd pctest test testdl testbooks launcher ports sdboot sdcard update sdtest data run clean

all: tools apps apps-sd launcher sdboot

# Python environment for the tools: system packages plus matplotlib.
venv:
	python3 -m venv --system-site-packages .venv
	.venv/bin/pip install matplotlib

# Host USB tools (need libusb-1.0-0-dev) and the flash loader.
tools:
	$(MAKE) -C $(FP)/pack_reloc
	$(MAKE) -C $(FP)/libc_server
	$(MAKE) -C $(SPD)
	$(CC) -O2 -std=c99 -DUSE_LIBUSB=1 -o $(SPD)/spd_dump_fast $(SPD)/spd_dump_fast.c -lusb-1.0
	$(MAKE) -C $(SPD)/fphelper
	$(MAKE) -C $(SPD)/custom_fdl $(ARM)

# USB mode: files are served from $(DATA) by libc_server.
apps:
	$(MAKE) -C $(FP)/apps $(ARM) OBJDIR=obj_usb NAME=apps

# SD card mode (sdboot): files are read from the card.
apps-sd:
	$(MAKE) -C $(FP)/apps $(ARM) OBJDIR=obj_sd NAME=apps_sd LIBC_SDIO=3

# The menu in a PC window (SDL2), files from the current directory:
# cd phone-data && ../fpdoom/apps/apps_pc.bin --res 240 320
pctest:
	$(MAKE) -C $(FP)/apps PCTEST=1 LOG=0 OBJDIR=obj_pc NAME=apps_pc

# Key-script scenarios in the emulator against golden screenshots (tests/run.py),
# on a card with the test books.
test: pctest testbooks
	$(PY) tests/run.py

# Long and real books for the tests (tests/downloads.txt), not in git.
testdl:
	mkdir -p dl/books
	grep -v '^#' tests/downloads.txt | while read name url; do \
		[ -f dl/books/$$name ] || curl -sSfL -o dl/books/$$name "$$url" || exit 1; \
	done

# The test card tests/out/card: phone-data plus the test books, checked by
# tests/test_books.py.
TESTBOOKS = tests/books/*.md tests/out/gen/*.epub tests/out/gen/*.fb2 tests/out/gen/*.html \
	$(wildcard dl/books/*)
testbooks: data
	rm -rf tests/out/card tests/out/gen
	mkdir -p tests/out
	cp -r $(DATA) tests/out/card
	mkdir -p tests/out/card/state
	$(PY) tests/make_books.py tests/out/gen
	$(PY) tools/mkbook.py tests/out/card $(TESTBOOKS)
	$(PY) tests/test_books.py tests/out/card $(TESTBOOKS)

# Boot launcher (fpbin/fpmain.bin): runs a program by the key held at power on,
# the table is fpdoom/launcher/config.txt.
launcher:
	$(MAKE) -C $(FP)/launcher $(ARM) OBJDIR=obj

# Optional programs for the SD card, as dir/name under fpdoom/: built as
# fpdoom/dir/name.bin, copied to fpbin/name.bin. Each needs its sources
# downloaded first, see README.md; games/ holds their data files.
PORTS = fpdoom/doom gnuboy/gnuboy infones/infones snes9x/snes9x puzzles/puzzles

ports:
	for p in $(PORTS); do \
		$(MAKE) -C $(FP)/$${p%/*} $(ARM) OBJDIR=obj_sd NAME=$${p#*/} LIBC_SDIO=3 || exit 1; \
	done

# SD boot loader for SC6531E (CHIP=1), installed into the firmware by sdboot/README.
sdboot:
	$(MAKE) -C $(FP)/sdboot CHIP=1 $(ARM)

# Card image in sdcard/: copy its contents to the root of a FAT32 card.
# Ports are added when built (make ports), games/ when it exists.
sdcard: apps-sd launcher data
	rm -rf sdcard && mkdir -p sdcard/fpbin sdcard/state
	cp -r $(DATA)/. sdcard/
	cp $(FP)/launcher/launcher.bin sdcard/fpbin/fpmain.bin
	cp $(FP)/launcher/config.txt sdcard/fpbin/config.txt
	cp $(FP)/apps/apps_sd.bin sdcard/fpbin/apps.bin
	for p in $(PORTS); do \
		if [ -f $(FP)/$$p.bin ]; then cp $(FP)/$$p.bin sdcard/fpbin/; fi; \
	done
	if [ -d games ]; then cp -r games sdcard/; fi

# Copy the card image to the phone connected as USB storage. books/ and decks/ are
# generated, so files missing from the image are deleted there; cards.txt and
# progress.txt in the card root are written by the phone and stay untouched.
# The debug log (LOG=1) is copied from the card to logs/log.txt.
# rsync copies fpbin/ in name order, so config.txt lands before fpmain.bin.
# The card is synced and unmounted even when a copy fails.
update: sdcard
	@dev=/dev/disk/by-label/$(CARD_LABEL); \
	if [ ! -e $$dev ]; then \
		echo "Подключите телефон кабелем и выберите режим накопителя (Ctrl-C — отмена)..."; \
		while [ ! -e $$dev ]; do sleep 1; done; \
	fi; \
	mnt=$$(findmnt -n -f -o TARGET $$dev) || { udisksctl mount -b $$dev >/dev/null && mnt=$$(findmnt -n -f -o TARGET $$dev); } || exit 1; \
	ok=1; \
	if [ -f $$mnt/log.txt ]; then mkdir -p logs && cp $$mnt/log.txt logs/log.txt; fi; \
	rsync -r --checksum --itemize-changes sdcard/ $$mnt/ && \
	rsync -r --checksum --itemize-changes --delete sdcard/books/ $$mnt/books/ && \
	rsync -r --checksum --itemize-changes --delete sdcard/decks/ $$mnt/decks/ || ok=0; \
	sync; udisksctl unmount -b $$dev >/dev/null; \
	if [ $$ok = 1 ]; then echo "Готово: телефон можно отключать."; \
	else echo "Ошибка записи на карту: запустите make update ещё раз." >&2; exit 1; fi

# Run sdboot from RAM over USB: tests the card without writing the flash.
sdtest: sdboot
	$(SPD)/spd_dump_fast --wait 600 fdl $(FP)/sdboot/sdboot1.bin 0x40004000

# Dictionary, level decks and the books from sources/: books in a folder of
# sources/books/ get the folder name as their shelf category.
data:
	mkdir -p $(DATA)
	$(PY) tools/mkdict.py $(DATA)/dict.bin sources/lexicon $(FP)/apps/words.txt
	$(PY) tools/mkdecks.py sources/lexicon $(DATA)
	rm -rf $(DATA)/books
	$(PY) tools/mkbook.py $(DATA) $(wildcard sources/books/*.*)
	for d in sources/books/*/; do \
		$(PY) tools/mkbook.py $(DATA) --cat "$$(basename "$$d")" "$$d"* || exit 1; \
	done

# Load the menu into RAM over USB (hold DIAL while plugging in).
run: apps data
	cd $(DATA) && ../$(SPD)/spd_dump_fast --wait 600 \
		fdl ../$(SPD)/custom_fdl/nor_fdl1.bin 0x40004000 fdl ../$(FP)/apps/apps.bin ram && \
		../$(FP)/libc_server/libc_server -- --bright 60 apps --day $$(( $$(date +%s) / 86400 ))

clean:
	$(RM) -r $(FP)/apps/obj_usb $(FP)/apps/obj_sd $(FP)/apps/obj_pc $(FP)/apps/apps.bin $(FP)/apps/apps_sd.bin $(FP)/apps/apps_pc.bin sdcard $(DATA) tests/out
	$(RM) -r $(FP)/launcher/obj $(FP)/launcher/launcher.bin
