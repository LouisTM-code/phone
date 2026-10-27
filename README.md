# Свои программы для кнопочного телефона BQ 3590 Step XXL+

Меню с программами для телефона на чипе Spreadtrum SC6531E (экран 240×320, 4 МБ ОЗУ).
Работает с SD-карты без компьютера (sdboot) или по USB-кабелю. Основа — проект друга (`v1_os`),
пересобранный под Linux.

| Программа | Что умеет |
|---|---|
| Книги | чтение английских книг, перевод слова по нажатию, закладки |
| Карточки | интервальное повторение слов как в Anki, наборы по уровням A1-C1, обмен с Anki |
| Словарь | англо-русский учебный словарь на ~4100 слов: часть речи, уровень, значения, примеры |
| Шахматы | 11 уровней силы (~300-1600), подсказка, отмена хода |
| Змейка | классическая игра |

## Сборка

Нужны: Linux, Android NDK r25b (`~/opt/android-ndk-r25b`, путь меняется через `NDK=`),
`libusb-1.0-0-dev`, Python 3. Один раз: `sudo sh sudo/01_udev_spd.sh` (доступ к телефону по USB).

| Команда | Что делает |
|---|---|
| `make tools` | утилиты USB: `spd_dump`, `spd_dump_fast`, `libc_server`, `fphelper`, `nor_fdl1.bin` |
| `make apps` / `make apps-sd` | программа для работы по кабелю / с SD-карты |
| `make data` | словарь, наборы слов и книги из `sources/` в `phone-data/` |
| `make sdcard` | содержимое SD-карты в `sdcard/` |
| `make run` | загрузить меню в память телефона по кабелю |
| `make sdtest` | запустить меню с карты через кабель, ничего не записывая во flash |

## Запуск с SD-карты

sdboot уже установлен во flash (подробности и команда удаления: `docs/hardware.md`).

1. Карта: FAT32 в первом разделе MBR. Через телефон в режиме накопителя:
   `sudo sh sudo/02_format_sd_fat32.sh` (стирает карту).
2. `make sdcard`, затем скопировать содержимое `sdcard/` в корень карты.
3. Включить телефон, зажав левую софт-клавишу (или `1`, `4`). Без клавиши грузится обычная прошивка.

Обновление: заменить на карте `fpbin/fpmain.bin` (программа) или книги. Прогресс хранится
в `progress.txt` и `cards.txt` на карте.

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

- `fpdoom/apps/` — программы (C): меню, книги, карточки, словарь, шахматы, змейка
- `tools/` — конвертеры на Python (словари, книги, колоды Anki); `mkfont.swift`, `mkpieces.swift`
  генерируют шрифт и фигуры и работают только на macOS
- `sources/lexicon/` — исходный текст словаря (формат: `sources/LEXICON_FORMAT.md`), `sources/books/` — книги A2-B2
- `docs/hardware.md` — измеренные характеристики телефона, дамп flash, sdboot
- `sudo/` — скрипты, которые нужно запускать под root
- `fpdoom/`, `spreadtrum_flash/` — библиотеки Ильи Курдюкова (public domain):
  [fpdoom](https://github.com/ilyakurdyukov/fpdoom) @ 296795c,
  [spreadtrum_flash](https://github.com/ilyakurdyukov/spreadtrum_flash) @ 3d05825.
  Изменения: исправлена сортировка в `pack_reloc.c`, добавлены `fpdoom/apps/` и `spd_dump_fast.c`
  (частый опрос USB и повтор рукопожатия с загрузчиком).

Словарь и книги написаны с помощью ИИ. Дамп прошивки лежит вне репозитория (`../backup_flash/`).
