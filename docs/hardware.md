# BQ-3590 Step XXL+ — measured hardware

Measured 2026-10-27 with fpdoom `fptest` loaded into RAM over USB (battery inserted).
Raw output: `logs/2026-10-27_fptest_full.log`, `logs/2026-10-27_fptest_keypad.log`.

## Chip and memory

| Item | Value |
|---|---|
| Chip | SC6531E, `chip_id 0x65620001`, ver `0xa0002` |
| CPU | ARM926EJ-S (`0x41069265`), cache `0x1d112112` |
| Heap (`malloc heap`) | 4 139 816 bytes (~3.95 MB) |
| Flash | GigaDevice GD25LQ32B, 4 MB (`sfc id 0xc86016`), read check OK |
| Boot key | DIAL (green) |

## Firmware and flash dump

| Item | Value |
|---|---|
| Firmware | `C75_F19_BQ3590_20250808_V105` (08-08-2025) |
| Dump | `../../backup_flash/flash_4mb_2026-10-27.bin` (outside the repo), SHA256 `4df5434c…846c05`; a second read (`…_read2.bin`) is byte-identical |
| Flash ID in the bootloader table (`0x4bc`) | `c8 60 16` (GigaDevice GD25LQ32B) |

Compared with the friend's dump (`v1_os/backup/flash.bin`, 2026-10-26, archive since deleted): the code area
`0x10000`-`0x3affff` is identical, user data `0x3b0000`-`0x3fffff` differs, and the flash ID at
`0x4bc` is `ef 40 16` (Winbond W25Q32). It is a different unit (hardware revision) with the same
firmware, so it is **not** a restore image for this phone, and its measurements need re-checking here.

## sdboot (installed 2026-10-27)

Instructions from `fphelper flash.bin sdboot` for this firmware; sdboot keys from `fphelper scan`:
LSOFT, 1, 4 (hold while powering on). Without a key the stock firmware boots.

```
# install/update (erase_flash asks for "yes" on stdin)
spd_dump fdl nor_fdl1.bin 0x40004000 \
  write_word fw+0x24 0x3ae000 \
  erase_flash fw+0x3ae000 4K \
  write_data fw+0x3ae000 0 0 sdboot1.bin \
  write_word fw+0x3ae004 0x95a1c

# remove
spd_dump fdl nor_fdl1.bin 0x40004000 \
  write_word fw+0x24 0x95a1c \
  erase_flash fw+0x3ae000 4K
```

Read back after install: `../../backup_flash/flash_after_sdboot_2026-10-27.bin`. Against the
original dump only `0x24` (start vector), `0x3ae000`-`0x3aefff` (sdboot) and user data
`>=0x3b0000` differ.

SD card: fpdoom's microfat needs an MBR partition table with FAT32 in partition 1
(a card formatted as a whole-disk FAT32 without partitions is not found). Formatting through the
phone's USB storage mode: `echo type=c | sfdisk /dev/sdX`, then `mkfs.vfat -F 32 -n BQ3590 /dev/sdX1`.

## Boot keys and the launcher (2026-10-27)

Measured with a probe program run by sdboot from the card:

- A program started by sdboot sees the keys still held from power on in `KEYPAD_BASE->key_status`:
  one byte per key, up to 4 (`0x80` = pressed, low bits = row<<4|col; `0x77` = empty slot).
  Extra keys show as pressed ~30 ms after start, so the launcher waits 100 ms.
- The first key must be an sdboot key: LSOFT (`0x10`), 1 (`0x01`) or 4 (`0x11`). Any other key may
  be held with it (seen: 4+2, 1+8, 4+*, LSOFT+2, 4+8+9). LSOFT combinations sometimes boot the stock
  firmware instead (sdboot side).
- Keys pressed while the power key is held come as events with bit 15 set.

`fpdoom/launcher/` (`fpbin/fpmain.bin`) picks the item of `fpbin/config.txt` named after the held
keys and chain-loads it with fpmenu's loader; the program then gets all of the RAM. Failures:
missing config, item or binary, or a binary under 256 bytes: exit and reboot to the stock firmware.
Known limitation (fpmenu's `readbin.c`): an SD read error while loading the chosen binary hangs
with the screen dark; pull the battery. A truncated binary is not detected either.

## Display

| Item | Value |
|---|---|
| Controller | ILI9340 (`LCD id 0x009340`) → 240x320 panel |
| LCD clock | 104 MHz |
| Full-frame refresh | 17.7 ms, ~56 fps |

## Timing and I/O

| Item | Value |
|---|---|
| Timer | `sys_wait_us(100000)` = 100 ms |
| USB raw read / write | 1 MB in 1387 / 1608 ms (~0.72 / ~0.62 MB/s) |
| SD raw read | 1 MB in 277 ms (~3.6 MB/s), high speed; depends on the card used |

## Keys

| Key | Event |
|---|---|
| `0`-`9`, `*` (STAR), `#` (HASH) | Codes `0x30`-`0x39`, `0x2a`, `0x23`, press and release |
| UP / DOWN / LEFT / RIGHT / CENTER | `0x04` / `0x05` / `0x06` / `0x07` / `0x0d` |
| LSOFT / RSOFT | `0x08` / `0x09` |
| DIAL (green) | `0x01` |
| Red (power/end) alone | No event |
| Red + key | Key code with bit 15 set (`POWER + 0x35`) |
| Volume / side keys | None |

fpdoom docs: holding the power key ~7 s powers the device off (not tested).

## Session notes

- In `keypad` mode `fptest` draws nothing on screen; progress is visible only in the log.
- A phone still running a loaded program stays enumerated as `1782:4d00`; wait for it to
  disappear before starting a new `spd_dump`, otherwise it fails with `LIBUSB_ERROR_TIMEOUT`.
- Two sessions dropped with `LIBUSB_ERROR_IO` while the cable was replugged; the kernel log also
  showed one `device descriptor read/64, error -71`.
