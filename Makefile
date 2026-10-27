# BQ-3590 apps (friend's v1_os, built on Linux).
# Override machine paths on the command line if needed.
NDK ?= $(HOME)/opt/android-ndk-r25b
NDK_BIN = $(NDK)/toolchains/llvm/prebuilt/linux-x86_64/bin
ARM = TOOLCHAIN=$(NDK_BIN)/llvm CC=$(NDK_BIN)/armv7a-linux-androideabi21-clang NM=$(NDK_BIN)/llvm-nm

FP = fpdoom
SPD = spreadtrum_flash
DATA = phone-data
PY = python3
# volume label set by sudo/02_format_sd_fat32.sh
CARD_LABEL ?= BQ3590

.PHONY: all tools apps apps-sd launcher doom sdboot sdcard update sdtest data run clean

all: tools apps apps-sd launcher sdboot

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

# Boot launcher (fpbin/fpmain.bin): runs a program by the key held at power on,
# the table is fpdoom/launcher/config.txt.
launcher:
	$(MAKE) -C $(FP)/launcher $(ARM) OBJDIR=obj

# Optional: Doom for the SD card. Needs the sources in fpdoom/doom_src and
# games/doom1/doom1.wad, see README.md.
doom:
	$(MAKE) -C $(FP)/fpdoom $(ARM) OBJDIR=obj_sd NAME=doom LIBC_SDIO=3

# SD boot loader for SC6531E (CHIP=1), installed into the firmware by sdboot/README.
sdboot:
	$(MAKE) -C $(FP)/sdboot CHIP=1 $(ARM)

# Card image in sdcard/: copy its contents to the root of a FAT32 card.
# Doom is added when built (make doom) and games/ exists.
sdcard: apps-sd launcher data
	rm -rf sdcard && mkdir -p sdcard/fpbin
	cp -r $(DATA)/. sdcard/
	cp $(FP)/launcher/launcher.bin sdcard/fpbin/fpmain.bin
	cp $(FP)/launcher/config.txt sdcard/fpbin/config.txt
	cp $(FP)/apps/apps_sd.bin sdcard/fpbin/apps.bin
	if [ -f $(FP)/fpdoom/doom.bin ]; then cp $(FP)/fpdoom/doom.bin sdcard/fpbin/doom.bin; fi
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

# Dictionary, level decks and the bundled books from sources/.
data:
	mkdir -p $(DATA)
	$(PY) tools/mkdict.py $(DATA)/dict.bin sources/lexicon $(FP)/apps/words.txt
	$(PY) tools/mkdecks.py sources/lexicon $(DATA)
	$(PY) tools/mkbook.py $(DATA) samples/*.txt sources/books/*.txt

# Load the menu into RAM over USB (hold DIAL while plugging in).
run: apps data
	cd $(DATA) && ../$(SPD)/spd_dump_fast --wait 600 \
		fdl ../$(SPD)/custom_fdl/nor_fdl1.bin 0x40004000 fdl ../$(FP)/apps/apps.bin ram && \
		../$(FP)/libc_server/libc_server -- --bright 60 apps --day $$(( $$(date +%s) / 86400 ))

clean:
	$(RM) -r $(FP)/apps/obj_usb $(FP)/apps/obj_sd $(FP)/apps/apps.bin $(FP)/apps/apps_sd.bin sdcard $(DATA)
	$(RM) -r $(FP)/launcher/obj $(FP)/launcher/launcher.bin
