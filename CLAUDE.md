# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

App menu (books with word translation, flashcards, dictionary, notes, quizzes) and party games (Alias, Spy, Odd one, Bomb) for the
**BQ-3590 Step XXL+** feature phone (Unisoc **SC6531E**), bare-metal C on the
[fpdoom](https://github.com/ilyakurdyukov/fpdoom) runtime. Based on a friend's macOS project
(`v1_os`), rebuilt on Linux. While an app runs the phone OS is not active.

## Commands

See `README.md`. Key targets: `make tools`, `make apps` (USB, `LIBC_SDIO=0`), `make apps-sd`
(SD card, `LIBC_SDIO=3`), `make ports`, `make data`, `make sdcard`, `make update-data` (books, decks, dictionary to the
phone's card over USB storage), `make update-code` (`fpbin/` only), `make update` (both), `make run`, `make sdtest`. Python tools run in `.venv` (`make venv`, matplotlib).

Tests without the phone: `make test` builds the PC version (`make pctest`, SDL2), the test card
`tests/out/card` (`make testbooks`: phone-data without its books (not built) plus the test books, `tests/books/rich.md` and
`dl/books/attention.pdf` from `make testdl`, checked by `tests/test_books.py`; the generated
EPUB/FB2/HTML are checked in `tests/out/gen`, not put on the card) and runs `tests/scenarios/*.keys`
in the emulator against `tests/golden/` (`tests/run.py`, `--update` to accept new screenshots after
looking at them). Key scripts: `fpdoom/pctest/syscode.c`; `fpdoom/apps/pccard.c` enforces the card's
limits and counts reads and heap (`#! read`/`#! peak` budgets in scripts). Run `make apps apps-sd`
too: the ARM link catches libc functions the phone lacks.
`phone-data/` and `sdcard/` are generated; never edit them by hand.
`archive/chess_snake/` holds the removed chess and snake apps; it is not built.

Books: `tools/mkbook.py` lays a book out for the reader's page area (232x292, must match
`reader.c`) into `books/<name>.bk` (format in `tools/bookfile.py`), `books/index.txt` and
`books/covers.bin`; a folder of `sources/books/` is a shelf category. The phone reads a page at a
time (`book.c`). A PDF with text (typeset or OCR) is read as text by `tools/pdfparse.py` (pdftohtml
-xml: headings by size and bookmarks, paragraphs, lists; pictures and tables cut out of the drawn
page); scanned PDF and DjVu are reflowed into page-size pictures by `dl/k2pdfopt` (the author's static
Linux build with DjVuLibre; Ubuntu's package crashes), no search, contents or title (the file name).
A book is built again only when its source or the tools of its kind are newer (`tools_time`).
The card is updated by `tools/cardsync.py`: by name and size, never reading it back, deleting
nothing but the books marked on the phone ("Удалить", the 6th field of `state/<id>.pos`), whose
sources go to `sources/deleted/`; books put on the card otherwise stay on its shelf. Reader state lives in `state/` (made by `make sdcard`):
`<id>.pos` (a book's place, one file per book), `<id>.txt` (bookmarks, quotes), `reader.txt`
(theme, screen and keypad brightness). The notes app (`notes.c`) keeps `state/notes.txt`, a note per line, and shows `fpbin/config.txt` read-only.
Flashcards (`cards.c`), decks as in ReWord: `tools/mkdecks.py` makes `decks/<id>.txt` (levels, `phrasal`,
`idioms` from the lexicon's `phr.` lines, topics from `sources/decks/*.apkg|.txt`) and `decks/index.txt`
("file, name, group, words, kind": kind `own` is a topic with meanings of its own); the deck "my" is the
words added from the dictionary and books. `cards.txt` (format v2, also `tools/anki.py`): a header line,
`#decks` (the decks that are on), then a card per line, "front back due ivl ease reps lapses example
decks [flags]" (decks "A1,my"; flags `m` a new meaning shown once, `n` to learn first). One word, one
card: the decks a word is in share its schedule. New words come from the decks that are on, by turns,
made cards when a session starts; a deck file read (session, turning a deck on, its word list) marks the
cards of its words, and an `own` deck adds a meaning the card hasn't got (a learned card then comes once
today, its interval kept). Learned is an interval of 21+ days; a deck's progress is learned + known.
The phrases are headwords of `dict.bin` too ("give up", with the examples of its word); the reader
offers a phrase made of the word and the next words of its sentence ("gave it up"), `cards_cloze()` and
`has_form()` in `tools/mkdict.py` find such phrases in examples the same way.
Quizzes (`quiz.c`): a question and four answers, rounds of `state/quiz.txt`'s length and direction,
statistics in `state/<set>.qz`, no schedule. A set is a tab-separated table `quiz/<set>.txt` (format in
`tools/mkquiz.py`: a `!mode` line is a topic of two columns asked both ways with one statistics,
`!group` the column to pick a part by, `!image` a picture column, `!icon` a column of signs drawn with
another column's text) listed in `quiz/index.txt`; `make data` builds them from `sources/quiz/`, drawing
the pictures into `quiz/<set>.bin`. A new set needs no code: a file and its index line. The currency
signs are pictures made once by `tools/mksigns.py` from `sources/quiz/signs/signs.txt`.
The party games are a program of their own, `fpbin/party.bin` (`make party`, `LOG=0`; on the PC
`party_pc.bin`, a test runs it by `#! app party`), started only by the key `1`, so the menu
doesn't hold their memory. `party.c` is its menu and shared code (the signal by `sys_vibrate` or
the keypad light, kept with the random seed in `state/party.txt`); `alias.c` takes its words from
`decks/` (all of them, or one); `roles.c` (Spy and Odd one: hidden roles, a table of modes per game) and `bomb.c` take
their cards from `party/*.txt` (copied from `sources/party/` by `make data`; a pair of close cards
is "card<TAB>card"). Each game keeps its settings in `state/<game>.txt`.

Card layout: `fpbin/fpmain.bin` is the boot launcher (`fpdoom/launcher/`), which runs a program
from `fpbin/` by the keys held at power on, per `fpdoom/launcher/config.txt` (fpmenu syntax,
captions are key names: `1+8`). Ports (`make ports`, list in `PORTS` of the Makefile) are
optional; their sources and `games/` are not in git (README.md). They link the runtime of
`fpdoom/fpdoom/` too: after changing `entry.c` or `syscode.*` (a new option such as `--kpled`)
run `make ports` again, an old port exits on an option it doesn't know. The puzzles port draws
its menus through `fpdoom/term/`; floats and math.h come from `fpdoom/softfp/`.
`fpdoom/*/helper.make` downloads a port's pinned sources.

## Phone access

- sdboot is installed in the flash (2026-10-27): power on holding LSOFT/1/4 runs
  `fpbin/fpmain.bin` from the SD card with args from `fpbin/config.txt`.
- USB/BROM mode (battery out/in, hold DIAL, plug USB): run `spd_dump` in the background and let the
  user do the physical steps. A phone still running a program stays enumerated as `1782:4d00`;
  wait for it to disappear first. `libc_server` relays the app's `printf` (debug log).
- `spd_dump erase_flash` asks for `yes` on stdin; pipe it, or a background run stops half-way.
- Anything that needs root is for the user to run; never run sudo.
- Flash writes are hard to reverse: confirm with the user first. Restore image and sdboot
  install/remove commands: `docs/hardware.md`.

## Platform constraints

- ARM926EJ-S 208 MHz: no FPU, no hardware divide; the apps use integer arithmetic only.
- Heap ~3.95 MB (measured). Screen 240x320 (ILI9340), but read it from `sys_data.display`.
- Keys: red (power) key alone sends no event, only as a modifier (bit 15). No volume keys.
  Keypad backlight: `--kpled N` in `fpbin/config.txt` (`sys_kpled`, ANA `0x82001540`, found in the
  stock firmware), on while a program runs.
- Register work (stock firmware reverse engineering, tests on the phone): `docs/registers.md` is the
  log of what was searched, tried and found; add every attempt there, failed ones too.
  Codes: `docs/hardware.md`.
- No readdir, battery level or power-off in the fpdoom API. The flashcard day (`app_today`) is read
  from the clock chip (`sys_rtc_day`, added to `fpdoom/fpdoom/syscode.c`) unless `--day` is given;
  the PC build has no clock, a test sets the day with a `#! day N` line (and can start from a
  prepared `cards.txt` of `tests/cards/` with `#! cards NAME`).
- SD card files (`fpdoom/fpdoom/microfat.c`): fopen "r"/"w"/"+" only (no append), a new file
  needs an 8.3 name, no mkdir/rename/remove; long names match in ASCII only (book files get
  transliterated names). Writes are whole-file rewrites.
- Font: Terminus 8x16, 528 glyphs, regular and bold (`font.h` from `tools/mkfont.py --full`): the
  console font's 512 and the IPA of transcriptions (`textclean.IPA`), some drawn in `mkfont.py`.
  A dictionary article starts with its transcription line ("/grɑːsp/"); the cards show it under
  the word (`dict_ipa`, the dictionary opened by the cards too).
- Look: `ui.h` holds the palette and the app colours, `icons.c` the 16x16 icons; screens draw
  through `ui_header`, `ui_footer`, `ui_tile`, `ui_pick` and `ui_grid` (the main and party menus),
  the selection is the app's colour (`ui_app`) with `UI_INK` text. The reading page keeps its
  own colours (`PAGE_*` in `reader.c`).
- The phone's libc lacks fgets/strstr/sscanf/rename/remove/...: the apps use the helpers in
  `fpdoom/apps/ui.h`, ports link `fpdoom/term/port_libc.c`. No float hardware: ports that use
  floats include `../softfp/softfp.make`.

## Debug log (temporary)

`fpdoom/apps/log.c` writes memory, stack and timings to `log.txt` on the card (stdout over USB);
`make update-code` copies it to `logs/log.txt`. Calls go through `log.h` macros only. Remove: build with
`LOG=0` (after `make clean`), or delete `log.c`/`log.h`, the `LOG` block in `fpdoom/apps/Makefile` and
the `log_*`/`LOG_*` lines in `main.c`, `reader.c`, `shelf.c`, `marks.c`, `notes.c`, `cards.c`, `dictfile.c`. The PC
build (`make pctest`) uses `LOG=0`: `log.c` reads the phone's stack and heap directly.
