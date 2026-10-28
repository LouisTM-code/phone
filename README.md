# Свои программы для кнопочного телефона BQ 3590 Step XXL+

Меню с программами для телефона на чипе Spreadtrum SC6531E (экран 240×320, 4 МБ ОЗУ).
Работает с SD-карты без компьютера (sdboot) или по USB-кабелю. Основа — проект друга (`v1_os`),
пересобранный под Linux.

| Программа | Что умеет |
|---|---|
| Книги | чтение английских книг, перевод слова по нажатию, закладки |
| Карточки | интервальное повторение слов как в Anki, наборы по уровням A1-C1, обмен с Anki |
| Словарь | англо-русский учебный словарь на ~4100 слов: часть речи, уровень, значения, примеры |
| Doom | shareware Doom 1 (по желанию, без звука), порт fpdoom |
| Эмуляторы | Game Boy (gnuboy), NES (InfoNES), SNES (Snes9x) — порты fpdoom |
| Головоломки | 40 головоломок Саймона Тэтхэма |

## Сборка

Нужны: Linux, Android NDK r25b (`~/opt/android-ndk-r25b`, путь меняется через `NDK=`),
`libusb-1.0-0-dev`, Python 3. Один раз: `sudo sh sudo/01_udev_spd.sh` (доступ к телефону по USB).

| Команда | Что делает |
|---|---|
| `make tools` | утилиты USB: `spd_dump`, `spd_dump_fast`, `libc_server`, `fphelper`, `nor_fdl1.bin` |
| `make apps` / `make apps-sd` | программа для работы по кабелю / с SD-карты |
| `make data` | словарь, наборы слов и книги из `sources/` в `phone-data/` |
| `make ports` | игры и программы из таблицы выше (нужны исходники, см. «Игры и программы») |
| `make sdcard` | содержимое SD-карты в `sdcard/` |
| `make update` | собрать и записать на карту телефона, подключённого как накопитель |
| `make run` | загрузить меню в память телефона по кабелю |
| `make sdtest` | запустить меню с карты через кабель, ничего не записывая во flash |

## Запуск с SD-карты

sdboot уже установлен во flash (подробности и команда удаления: `docs/hardware.md`).

1. Карта: FAT32 в первом разделе MBR. Через телефон в режиме накопителя:
   `sudo sh sudo/02_format_sd_fat32.sh` (стирает карту).
2. `make sdcard`, затем скопировать содержимое `sdcard/` в корень карты.
3. Включить телефон, зажав клавишу. Без клавиши грузится обычная прошивка.

| Клавиша при включении | Что запускается |
|---|---|
| левая софт-клавиша (LSOFT) или `1` | меню |
| `4` | Doom |
| `1`+`2` | Game Boy: Tobu Tobu Girl Deluxe |
| `1`+`5` | Game Boy: Deadeus |
| `1`+`9` | головоломки |

Выбор делает загрузчик `fpbin/fpmain.bin` по таблице `fpdoom/launcher/config.txt` (копируется
на карту как `fpbin/config.txt`). Первой должна быть зажата LSOFT, `1` или `4` — только их видит
sdboot; вторую клавишу можно нажать сразу после первой. Если программы нет на карте, телефон
перезагружается в обычную прошивку.

Обновление: `make update`, затем подключить телефон кабелем в режиме накопителя. Книги и наборы
слов на карте заменяются содержимым `sources/` (лишние удаляются), прогресс в `progress.txt`
и `cards.txt` не трогается.

## Игры и программы

Исходники и данные игр не входят в репозиторий; файлы игр лежат в `games/` и копируются на
карту в `games/`. Один раз скачать исходники:

```
cd fpdoom
make -C gnuboy -f helper.make all patch
make -C infones -f helper.make all patch
make -C snes9x -f helper.make all patch
make -C puzzles -f helper.make
make -C softfp -f helper.make     # плавающая точка для ARM926 (compiler-rt)
cd ..
make ports update
```

Doom:

```
git clone --depth 1 https://github.com/id-Software/DOOM.git /tmp/DOOM
cp -r /tmp/DOOM/linuxdoom-1.10 fpdoom/doom_src
patch -d fpdoom/doom_src -p1 -i ../doom.patch
# doom1.wad (shareware 1.9, md5 f0cefca49926d00903cf57551d901abe) — из пакета Debian
# doom-wad-shareware: dpkg-deb -x doom-wad-shareware_*.deb x
mkdir -p games/doom1 && cp x/usr/share/games/doom/doom1.wad games/doom1/
```

Файлы игр (все бесплатные):

| Куда | Что | Откуда |
|---|---|---|
| `games/gb/tobudx.gb` | Tobu Tobu Girl Deluxe (бесплатно) | tangramgames.itch.io/tobu-tobu-girl-deluxe, файл `tobudx.gb` |
| `games/gb/deadeus.gb` | Deadeus 1.4 (бесплатно) | izma.itch.io/deadeus, `Deadeus.gb` из архива Regular Edition |

Свои ROM кладутся туда же; имя файла меняется в `fpdoom/launcher/config.txt`.

Управление в головоломках: стрелки — курсор, центр и `*` — действие, цифры — числа, RSOFT — отменить ход, LSOFT — меню.

## Запуск по кабелю

`make run`, затем: вынуть батарею, вставить, зажать зелёную кнопку и подключить кабель.
Файлы телефон читает с компьютера из `phone-data/`.

## Добавить книги и словари

```
python3 tools/mkbook.py phone-data КНИГА.epub        # .epub, .fb2, .txt
python3 tools/mkdict.py phone-data/dict.bin sources/lexicon fpdoom/apps/words.txt [СЛОВАРЬ.ifo]
python3 tools/anki.py import phone-data КОЛОДА.apkg
```

## Устройство

- `fpdoom/apps/` — программы (C): меню, книги, карточки, словарь
- `fpdoom/launcher/` — загрузчик: выбор программы по клавише при включении (код запуска из `fpdoom/fpmenu/`)
- `fpdoom/term/` — текстовый экран для меню портов, недостающие функции libc
- `fpdoom/softfp/` — плавающая точка и `math.h` (у ARM926 нет FPU, библиотека NDK — для ARMv7)
- `fpdoom/puzzles/` — порт головоломок: свой интерфейс к экрану и клавишам
- `tools/` — конвертеры на Python (словари, книги, колоды Anki, кириллица шрифта из
  `/usr/share/consolefonts/`: `mkfont.py`)
- `sources/lexicon/` — исходный текст словаря (формат: `sources/LEXICON_FORMAT.md`), `sources/books/` — книги A2-B2
- `archive/chess_snake/` — убранные из меню шахматы и змейка (не собираются)
- `docs/hardware.md` — измеренные характеристики телефона, дамп flash, sdboot
- `sudo/` — скрипты, которые нужно запускать под root
- `fpdoom/`, `spreadtrum_flash/` — библиотеки Ильи Курдюкова (public domain):
  [fpdoom](https://github.com/ilyakurdyukov/fpdoom) @ 296795c,
  [spreadtrum_flash](https://github.com/ilyakurdyukov/spreadtrum_flash) @ 3d05825.
  Изменения: исправлена сортировка в `pack_reloc.c`, добавлены `fpdoom/apps/`, `fpdoom/launcher/`, порты выше и `spd_dump_fast.c`
  (частый опрос USB и повтор рукопожатия с загрузчиком).

Словарь и книги написаны с помощью ИИ. Дамп прошивки лежит вне репозитория (`../backup_flash/`).
