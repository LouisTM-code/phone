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
# volume label of the card's FAT32 partition (mkfs.vfat -n)
CARD_LABEL ?= BQ3590

.PHONY: all venv tools apps apps-sd party pctest test testdl testbooks launcher ports sdboot sdcard update \
	update-data update-code sdtest data data-dict data-decks data-party data-quiz data-books run clean lexdl lexicon

all: tools apps apps-sd party launcher sdboot

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

# Party games (fpdoom/apps/party.c: Alias, Spy, Bomb), fpbin/party.bin: run by
# the key 1 at power on, not from the menu. No debug log: it would overwrite
# the menu's log.txt.
party:
	$(MAKE) -C $(FP)/apps $(ARM) OBJDIR=obj_party NAME=party LIBC_SDIO=3 LOG=0

# The menu in a PC window (SDL2), files from the current directory:
# cd phone-data && ../fpdoom/apps/apps_pc.bin --res 240 320 (party_pc.bin for the party games)
pctest:
	$(MAKE) -C $(FP)/apps PCTEST=1 LOG=0 OBJDIR=obj_pc NAME=apps_pc
	$(MAKE) -C $(FP)/apps PCTEST=1 LOG=0 OBJDIR=obj_party_pc NAME=party_pc

# Key-script scenarios in the emulator against golden screenshots (tests/run.py),
# on a card with the test books.
test: pctest testbooks
	$(PY) tests/run.py

# The PDF for the tests (tests/downloads.txt), not in git.
testdl:
	mkdir -p dl/books
	grep -v '^#' tests/downloads.txt | while read name url; do \
		[ -f dl/books/$$name ] || curl -sSfL -o dl/books/$$name "$$url" || exit 1; \
	done

# The test card tests/out/card: phone-data with the test books instead of its own,
# checked by tests/test_books.py. The EPUB, FB2 and HTML books are checked in
# tests/out/gen, not put on the card.
TESTBOOKS = tests/books/*.md $(wildcard dl/books/*)
GENBOOKS = tests/out/gen/*.epub tests/out/gen/*.fb2 tests/out/gen/*.html
testbooks: data-dict data-decks data-party data-quiz
	rm -rf tests/out/card tests/out/gen
	mkdir -p tests/out/card
	cp -r $(DATA)/dict.bin $(DATA)/decks $(DATA)/party $(DATA)/quiz tests/out/card/
	$(PY) tools/mkdecks.py sources/lexicon tests/out/card tests/decks
	$(PY) tests/test_decks.py tests/out/card
	mkdir -p tests/out/card/state tests/out/card/fpbin
	cp $(FP)/launcher/config.txt tests/out/card/fpbin/config.txt
	$(PY) tests/make_books.py tests/out/gen
	$(PY) tools/mkbook.py tests/out/gen $(GENBOOKS)
	$(PY) tests/test_books.py tests/out/gen $(GENBOOKS)
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
sdcard: apps-sd party launcher data
	rm -rf sdcard && mkdir -p sdcard/fpbin sdcard/state
	cp -a $(DATA)/. sdcard/
	cp $(FP)/launcher/launcher.bin sdcard/fpbin/fpmain.bin
	cp $(FP)/launcher/config.txt sdcard/fpbin/config.txt
	cp $(FP)/apps/apps_sd.bin sdcard/fpbin/apps.bin
	cp $(FP)/apps/party.bin sdcard/fpbin/
	for p in $(PORTS); do \
		if [ -f $(FP)/$$p.bin ]; then cp $(FP)/$$p.bin sdcard/fpbin/; fi; \
	done
	if [ -d games ]; then cp -r games sdcard/; fi

# The phone connected as USB storage: waits for its card and mounts it at $mnt;
# CARD_DONE syncs and unmounts it even when a step failed ($ok), then says so.
CARD = dev=/dev/disk/by-label/$(CARD_LABEL); \
	if [ ! -e $$dev ]; then \
		echo "Подключите телефон кабелем и выберите режим накопителя (Ctrl-C — отмена)..."; \
		while [ ! -e $$dev ]; do sleep 1; done; \
	fi; \
	mnt=$$(findmnt -n -f -o TARGET $$dev) || { udisksctl mount -b $$dev >/dev/null && \
		mnt=$$(findmnt -n -f -o TARGET $$dev); } || exit 1; \
	ok=1
CARD_DONE = sync; udisksctl unmount -b $$dev >/dev/null; \
	if [ $$ok = 1 ]; then echo "Готово: телефон можно отключать."; \
	else echo "Ошибка записи на карту: запустите команду ещё раз." >&2; exit 1; fi
# Data (tools/cardsync.py): the books marked on the phone are deleted from the card and
# their sources moved to sources/deleted/, then the data is built and written by name
# and size (the card is not read back). Nothing else is deleted from the card.
SYNC_DATA = $(PY) tools/cardsync.py deleted $$mnt $(DATA) sources/books sources/deleted && \
	$(MAKE) --no-print-directory data && \
	$(PY) tools/cardsync.py data $(DATA) $$mnt
# Code: the programs of fpbin/ are copied whole (a few MB), config.txt before
# fpmain.bin; games/ by name and size. The debug log (LOG=1) comes back to logs/log.txt.
SYNC_CODE = { [ ! -f $$mnt/log.txt ] || { mkdir -p logs && cp $$mnt/log.txt logs/log.txt; }; } && \
	mkdir -p $$mnt/fpbin $$mnt/state && \
	cp $(FP)/launcher/config.txt $$mnt/fpbin/config.txt && \
	cp $(FP)/launcher/launcher.bin $$mnt/fpbin/fpmain.bin && \
	cp $(FP)/apps/apps_sd.bin $$mnt/fpbin/apps.bin && \
	cp $(FP)/apps/party.bin $$mnt/fpbin/ && \
	for p in $(PORTS); do [ ! -f $(FP)/$$p.bin ] || cp $(FP)/$$p.bin $$mnt/fpbin/ || exit 1; done && \
	{ [ ! -d games ] || $(PY) tools/cardsync.py copy games $$mnt/games; }

# Books, decks, dictionary, quizzes and party lists to the phone's card.
update-data:
	@$(CARD); { $(SYNC_DATA); } || ok=0; $(CARD_DONE)

# The programs only, the data is not built.
update-code: apps-sd party launcher
	@$(CARD); { $(SYNC_CODE); } || ok=0; $(CARD_DONE)

# Both, the card mounted once.
update: apps-sd party launcher
	@$(CARD); { $(SYNC_CODE) && $(SYNC_DATA); } || ok=0; $(CARD_DONE)

# Run sdboot from RAM over USB: tests the card without writing the flash.
sdtest: sdboot
	$(SPD)/spd_dump_fast --wait 600 fdl $(FP)/sdboot/sdboot1.bin 0x40004000

# The free dictionaries tools/mklexicon.py makes the lexicon from (~3.5 GB, not in git).
TATOEBA = https://downloads.tatoeba.org/exports/per_language
CEFRJ = https://raw.githubusercontent.com/openlanguageprofiles/olp-en-cefrj/master
lexdl:
	mkdir -p dl/dict
	cd dl/dict && for u in $(TATOEBA)/eng/eng-rus_links.tsv.bz2 $(TATOEBA)/eng/eng_sentences.tsv.bz2 \
			$(TATOEBA)/rus/rus_sentences.tsv.bz2 $(CEFRJ)/cefrj-vocabulary-profile-1.5.csv \
			$(CEFRJ)/octanove-vocabulary-profile-c1c2-1.0.csv \
			https://kaikki.org/dictionary/English/kaikki.org-dictionary-English.jsonl; do \
		[ -f $$(basename $$u) ] || curl -sSfLO $$u || exit 1; \
	done
	cd dl/dict && [ -f mueller7.index ] || { apt-get download mueller7-dict && \
		dpkg-deb --fsys-tarfile mueller7-dict_*.deb | tar -x --strip-components=4 ./usr/share/dictd/; }

# The lexicon made from dl/dict by tools/mklexicon.py (the words of the CEFR-J lists and
# of sources/lexicon/); the dictionary is built from it.
LEXICON = sources/lexicon_auto
# Topic decks (Anki decks, tables) put by hand: they may not be shared, so not in git.
TOPICS = dl/decks
lexicon:
	rm -rf $(LEXICON)
	$(PY) tools/mklexicon.py dl/dict sources/lexicon $(LEXICON)

# Dictionary, decks (levels, phrases, the topics of sources/decks/), the party games' lists,
# the quiz sets and the books from sources/, made at once on several cores (mkbook.py
# builds several books at once too); books in a folder of sources/books/ get the folder
# name as their shelf category.
data:
	$(MAKE) --no-print-directory -j data-dict data-decks data-party data-quiz data-books

data-dict:
	mkdir -p $(DATA)
	$(PY) tools/mkdict.py --families sources/families.txt $(DATA)/dict.bin $(LEXICON) $(FP)/apps/words.txt

data-decks:
	mkdir -p $(DATA)
	rm -rf $(DATA)/decks && $(PY) tools/mkdecks.py sources/lexicon $(DATA) $(TOPICS)

data-party:
	mkdir -p $(DATA)
	rm -rf $(DATA)/party && cp -r sources/party $(DATA)/

data-quiz:
	mkdir -p $(DATA)
	rm -rf $(DATA)/quiz && $(PY) tools/mkquiz.py sources/quiz $(DATA)

data-books:
	mkdir -p $(DATA) sources/books
	$(PY) tools/mkbook.py $(DATA) sources/books

# Load the menu into RAM over USB (hold DIAL while plugging in).
run: apps data
	cd $(DATA) && ../$(SPD)/spd_dump_fast --wait 600 \
		fdl ../$(SPD)/custom_fdl/nor_fdl1.bin 0x40004000 fdl ../$(FP)/apps/apps.bin ram && \
		../$(FP)/libc_server/libc_server -- --bright 60 --kpled 50 apps --day $$(( $$(date +%s) / 86400 ))

clean:
	$(RM) -r $(FP)/apps/obj_usb $(FP)/apps/obj_sd $(FP)/apps/obj_pc $(FP)/apps/apps.bin $(FP)/apps/apps_sd.bin $(FP)/apps/apps_pc.bin
	$(RM) -r $(FP)/apps/obj_party $(FP)/apps/obj_party_pc $(FP)/apps/party.bin $(FP)/apps/party_pc.bin sdcard $(DATA) tests/out
	$(RM) -r $(FP)/launcher/obj $(FP)/launcher/launcher.bin
