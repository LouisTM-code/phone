# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

App menu (books with word translation, flashcards, dictionary) for the
**BQ-3590 Step XXL+** feature phone (Unisoc **SC6531E**), bare-metal C on the
[fpdoom](https://github.com/ilyakurdyukov/fpdoom) runtime. Based on a friend's macOS project
(`v1_os`), rebuilt on Linux. While an app runs the phone OS is not active.

## Commands

See `README.md`. Key targets: `make tools`, `make apps` (USB, `LIBC_SDIO=0`), `make apps-sd`
(SD card, `LIBC_SDIO=3`), `make ports`, `make data`, `make sdcard`, `make update` (copy to the phone's card
over USB storage), `make run`, `make sdtest`. Python tools run in `.venv` (`make venv`, matplotlib).

Tests without the phone: `make test` builds the PC version (`make pctest`, SDL2), the test card
`tests/out/card` (`make testbooks`: phone-data plus `tests/books/`, generated EPUB/FB2/HTML and
`dl/books/` from `make testdl`, checked by `tests/test_books.py`) and runs `tests/scenarios/*.keys`
in the emulator against `tests/golden/` (`tests/run.py`, `--update` to accept new screenshots after
looking at them). Key scripts: `fpdoom/pctest/syscode.c`; `fpdoom/apps/pccard.c` enforces the card's
limits and counts reads and heap (`#! read`/`#! peak` budgets in scripts). Run `make apps apps-sd`
too: the ARM link catches libc functions the phone lacks.
`phone-data/` and `sdcard/` are generated; never edit them by hand.
`archive/chess_snake/` holds the removed chess and snake apps; it is not built.

Books: `tools/mkbook.py` lays a book out for the reader's page area (232x292, must match
`reader.c`) into `books/<name>.bk` (format in `tools/bookfile.py`), `books/index.txt` and
`books/covers.bin`; a folder of `sources/books/` is a shelf category. The phone reads a page at a
time (`book.c`). PDF is reflowed into page-size pictures by `dl/k2pdfopt` (the author's static
Linux build; Ubuntu's package crashes). Reader state lives in `state/` (made by `make sdcard`):
`<id>.pos` (a book's place, one file per book), `<id>.txt` (bookmarks, quotes), `reader.txt`
(theme, brightness).

Card layout: `fpbin/fpmain.bin` is the boot launcher (`fpdoom/launcher/`), which runs a program
from `fpbin/` by the keys held at power on, per `fpdoom/launcher/config.txt` (fpmenu syntax,
captions are key names: `1+8`). Ports (`make ports`, list in `PORTS` of the Makefile) are
optional; their sources and `games/` are not in git (README.md). The puzzles port draws
its menus through `fpdoom/term/`; floats and math.h come from `fpdoom/softfp/`.
`fpdoom/*/helper.make` downloads a port's pinned sources.

## Phone access

- sdboot is installed in the flash (2026-10-27): power on holding LSOFT/1/4 runs
  `fpbin/fpmain.bin` from the SD card with args from `fpbin/config.txt`.
- USB/BROM mode (battery out/in, hold DIAL, plug USB): run `spd_dump` in the background and let the
  user do the physical steps. A phone still running a program stays enumerated as `1782:4d00`;
  wait for it to disappear first. `libc_server` relays the app's `printf` (debug log).
- `spd_dump erase_flash` asks for `yes` on stdin; pipe it, or a background run stops half-way.
- Anything that needs root goes into `sudo/NN_name.sh` for the user to run; never run sudo.
- Flash writes are hard to reverse: confirm with the user first. Restore image and sdboot
  install/remove commands: `docs/hardware.md`.

## Platform constraints

- ARM926EJ-S 208 MHz: no FPU, no hardware divide; the apps use integer arithmetic only.
- Heap ~3.95 MB (measured). Screen 240x320 (ILI9340), but read it from `sys_data.display`.
- Keys: red (power) key alone sends no event, only as a modifier (bit 15). No volume keys.
  Codes: `docs/hardware.md`.
- No readdir, RTC, battery level or power-off in the fpdoom API. In SD mode there is no `--day`
  argument, so flashcard scheduling (`app_today`) has no date yet (open issue).
- SD card files (`fpdoom/fpdoom/microfat.c`): fopen "r"/"w"/"+" only (no append), a new file
  needs an 8.3 name, no mkdir/rename/remove; long names match in ASCII only (book files get
  transliterated names). Writes are whole-file rewrites.
- Font: Terminus 8x16, 512 glyphs, regular and bold (`font.h` from `tools/mkfont.py --full`).
- The phone's libc lacks fgets/strstr/sscanf/rename/remove/...: the apps use the helpers in
  `fpdoom/apps/ui.h`, ports link `fpdoom/term/port_libc.c`. No float hardware: ports that use
  floats include `../softfp/softfp.make`.

## Debug log (temporary)

`fpdoom/apps/log.c` writes memory, stack and timings to `log.txt` on the card (stdout over USB);
`make update` copies it to `logs/log.txt`. Calls go through `log.h` macros only. Remove: build with
`LOG=0` (after `make clean`), or delete `log.c`/`log.h`, the `LOG` block in `fpdoom/apps/Makefile` and
the `log_*`/`LOG_*` lines in `main.c`, `reader.c`, `shelf.c`, `marks.c`, `cards.c`, `dictfile.c`. The PC
build (`make pctest`) uses `LOG=0`: `log.c` reads the phone's stack and heap directly.
