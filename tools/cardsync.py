#!/usr/bin/env python3
"""Brings the phone's card up to date with the files made on the PC.

Usage:
  cardsync.py deleted CARD DATA_DIR SOURCES DELETED
      The books marked on the phone for deletion (the 6th field of
      state/<id>.pos is 1): their .bk and .pos are removed from the card, their
      lines and covers from the card's books/index.txt and covers.bin, and
      their sources are moved from SOURCES to DELETED, so they don't come back.
      Bookmarks and quotes (state/<id>.txt) stay.
  cardsync.py data DATA_DIR CARD
      The dictionary, decks, quiz sets, party lists and books of DATA_DIR are
      written to the card where it hasn't got a file of that name and size. A
      file of the name but another size is removed and written again, said
      aloud. Nothing else on the card is removed: books put there otherwise
      stay, and keep their lines in books/index.txt (written last, after the
      books).
  cardsync.py copy SRC DST
      The files of SRC written to DST by name and size the same way (games/).
Only names and sizes are compared: the card is not read back.
"""
import os
import shutil
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mkbook  # noqa: E402

DATA = ["dict.bin", "decks", "party", "quiz", "books"]
INDEX, COVERS = "books/index.txt", "books/covers.bin"
THUMB = mkbook.CONF["thumb"][0] * mkbook.CONF["thumb"][1] * 2
SYNC_STEP = 4 << 20  # bytes written to the card between flushes (the progress bar steps)


def read_index(root):
    """[(line, fields)] of root/books/index.txt and the covers, one a line."""
    path = os.path.join(root, INDEX)
    if not os.path.exists(path):
        return [], []
    lines = [ln for ln in open(path, encoding="utf-8").read().split("\n") if ln]
    covers = open(os.path.join(root, COVERS), "rb").read() if os.path.exists(os.path.join(root, COVERS)) else b""
    return [(ln, ln.split("\t")) for ln in lines], \
        [covers[i * THUMB:(i + 1) * THUMB].ljust(THUMB, b"\0") for i in range(len(lines))]


def write_index(root, entries, covers):
    """Writes the index and the covers to root if they differ."""
    for name, data in ((INDEX, "".join(ln + "\n" for ln, _ in entries).encode("utf-8")),
                       (COVERS, b"".join(covers))):
        path = os.path.join(root, name)
        if not os.path.exists(path) or open(path, "rb").read() != data:
            open(path, "wb").write(data)


def deleted(card, data, sources, trash):
    entries, covers = read_index(card)
    keep, kept_covers, gone = [], [], set()
    for (line, f), cover in zip(entries, covers):
        pos = os.path.join(card, "state", f[4] + ".pos") if len(f) >= 5 else ""
        fields = open(pos, encoding="utf-8", errors="replace").read().strip().split("\t") \
            if pos and os.path.exists(pos) else []
        if len(fields) >= 6 and fields[5] == "1":
            for p in (os.path.join(card, f[0]), pos):
                if os.path.exists(p):
                    os.remove(p)
            print(f"удалена с карты: {f[1]} ({f[0]})")
            gone.add(f[0])
        else:
            keep.append((line, f))
            kept_covers.append(cover)
    if not gone:
        return
    write_index(card, keep, kept_covers)
    for src, _ in mkbook.sources([sources]):
        ext = next(e for e in mkbook.BOOK_EXTS if src.lower().endswith(e))
        if f"books/{mkbook.file_name(os.path.basename(src)[:-len(ext)])}.bk" in gone:
            dst = os.path.join(trash, os.path.relpath(src, sources))
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            shutil.move(src, dst)
            print(f"исходник перенесён: {src} -> {dst}")


def copy_files(src_root, dst_root, names, skip=()):
    """Writes the files under names (relative to src_root) missing from
    dst_root or of another size there, with a progress bar of the bytes."""
    plan = []
    for name in names:
        top = os.path.join(src_root, name)
        files = [top] if os.path.isfile(top) else \
            sorted(os.path.join(d, f) for d, _, fs in os.walk(top) for f in fs)
        for src in files:
            rel = os.path.relpath(src, src_root)
            if rel in skip:
                continue
            dst = os.path.join(dst_root, rel)
            size = os.path.getsize(src)
            if not os.path.exists(dst):
                plan.append((src, dst, f"новый: {rel} ({size} байт)", size))
            elif os.path.getsize(dst) != size:
                # FAT has no overwriting in place: the old file goes, the new one is written
                plan.append((src, dst, f"перезапись: {rel} ({os.path.getsize(dst)} -> {size} байт)", size))
    bar = Progress(sum(p[3] for p in plan))
    for src, dst, msg, _ in plan:
        bar.say(msg)
        if os.path.exists(dst):
            os.remove(dst)
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        with open(src, "rb") as fi, open(dst, "wb") as fo:
            while True:
                chunk = fi.read(SYNC_STEP)
                if not chunk:
                    break
                fo.write(chunk)
                # flushed to the card as it goes: the bar shows the card, not the cache
                fo.flush()
                os.fsync(fo.fileno())
                bar.add(len(chunk))
    bar.done()


class Progress:
    """A line "[####....] 45%  650/1450 МБ  1,1 МБ/с  осталось ~12 мин" at the
    bottom of a terminal; messages go above it. Nothing is drawn into a file."""

    def __init__(self, total):
        self.total, self.got, self.t0 = total, 0, time.time()
        self.tty = sys.stdout.isatty() and total > 0

    def say(self, msg):
        if self.tty:
            sys.stdout.write("\r\033[K")
        print(msg)
        self.draw()

    def add(self, n):
        self.got += n
        self.draw()

    def draw(self):
        if not self.tty:
            return
        part = self.got / self.total
        speed = self.got / max(time.time() - self.t0, 0.001)
        left = (self.total - self.got) / speed if self.got else 0
        mb = 1 << 20
        sys.stdout.write(f"\r\033[Kзапись: [{'█' * int(part * 20):░<20}] {part:4.0%}  "
                         f"{self.got // mb}/{self.total // mb} МБ  {speed / mb:.1f} МБ/с  "
                         f"осталось ~{left / 60:.0f} мин")
        sys.stdout.flush()

    def done(self):
        if self.tty:
            sys.stdout.write("\r\033[K")
            sys.stdout.flush()


def data(data_dir, card):
    copy_files(data_dir, card, [n for n in DATA if os.path.exists(os.path.join(data_dir, n))],
               skip={INDEX, COVERS})
    # the shelf: the books of the PC, then those only on the card
    entries, covers = read_index(data_dir)
    ours = {f[0] for _, f in entries}
    for (line, f), cover in zip(*read_index(card)):
        if f[0] not in ours and os.path.exists(os.path.join(card, f[0])):
            entries.append((line, f))
            covers.append(cover)
    write_index(card, entries, covers)


def main():
    a = sys.argv[1:]
    if a[:1] == ["deleted"] and len(a) == 5:
        deleted(*a[1:])
    elif a[:1] == ["data"] and len(a) == 3:
        data(*a[1:])
    elif a[:1] == ["copy"] and len(a) == 3:
        copy_files(a[1], a[2], sorted(os.listdir(a[1])))
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main()
