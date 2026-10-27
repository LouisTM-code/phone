# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

App menu (books with word translation, flashcards, dictionary) for the
**BQ-3590 Step XXL+** feature phone (Unisoc **SC6531E**), bare-metal C on the
[fpdoom](https://github.com/ilyakurdyukov/fpdoom) runtime. Based on a friend's macOS project
(`v1_os`), rebuilt on Linux. While an app runs the phone OS is not active.

## Commands

See `README.md`. Key targets: `make tools`, `make apps` (USB, `LIBC_SDIO=0`), `make apps-sd`
(SD card, `LIBC_SDIO=3`), `make data`, `make sdcard`, `make update` (copy to the phone's card
over USB storage), `make run`, `make sdtest`.
`phone-data/` and `sdcard/` are generated; never edit them by hand.
`archive/chess_snake/` holds the removed chess and snake apps; it is not built.

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

- ARM926EJ-S 208 MHz: no FPU, no hardware divide; integer arithmetic only.
- Heap ~3.95 MB (measured). Screen 240x320 (ILI9340), but read it from `sys_data.display`.
- Keys: red (power) key alone sends no event, only as a modifier (bit 15). No volume keys.
  Codes: `docs/hardware.md`.
- No readdir, RTC, battery level or power-off in the fpdoom API. In SD mode there is no `--day`
  argument, so flashcard scheduling (`app_today`) has no date yet (open issue).
- The phone's libc has no fgets/strstr; see helpers in `fpdoom/apps/ui.h`.

## Debug log (temporary)

`fpdoom/apps/log.c` writes memory, stack and timings to `log.txt` on the card (stdout over USB);
`make update` copies it to `logs/log.txt`. Calls go through `log.h` macros only. Remove: build with
`LOG=0` (after `make clean`), or delete `log.c`/`log.h`, the `LOG` block in `fpdoom/apps/Makefile` and
the `log_*`/`LOG_*` lines in `main.c`, `reader.c`, `cards.c`, `dictfile.c`.
