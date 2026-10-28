#!/usr/bin/env python3
"""Makes the EPUB, FB2 and HTML test books in OUT_DIR from tests/books/.

Usage: make_books.py OUT_DIR
"""
import base64
import os
import shutil
import sys
import zipfile

BOOKS = os.path.join(os.path.dirname(os.path.abspath(__file__)), "books")
IMG = os.path.join(BOOKS, "img")

CH1 = """<h1>Глава первая</h1>
<div class="epigraph"><p>Всё смешалось в доме Облонских.</p><p class="author">Л. Толстой</p></div>
<p>Первый абзац главы с <b>жирным</b> и <i>курсивным</i> словом и сноской<a epub:type="noteref" href="#n1">1</a>.
Второе предложение того же абзаца, достаточно длинное для переноса.</p>
<p>Второй абзац: H<sub>2</sub>O и E = mc<sup>2</sup>.</p>
<figure><img src="img/chart.png" alt="chart"/><figcaption>Рис. 1. Диаграмма в EPUB</figcaption></figure>
<h2>Раздел с таблицей</h2>
<table><thead><tr><th>Страна</th><th>Столица</th></tr></thead>
<tbody><tr><td>Франция</td><td>Париж</td></tr><tr><td>Италия</td><td>Рим</td></tr></tbody></table>
<ol><li>Пункт один</li><li>Пункт два<ul><li>вложенный</li></ul></li></ol>
<pre>code line 1
    indented line 2</pre>
<aside epub:type="footnote" id="n1"><p>1. Текст сноски.</p></aside>"""

CH2 = """<h1>Chapter Two</h1>
<p>An English chapter with a <em>formula</em>
<math display="block" alttext="a^2 + b^2 = c^2"><mi>a</mi></math> in between.</p>
<p>Line one<br/>line two<br/>line three</p>
<blockquote><p>A quotation in English.</p></blockquote>
<hr/>
<p>The end.</p>"""


def xhtml(body):
    return f"""<?xml version="1.0" encoding="utf-8"?>
<html xmlns="http://www.w3.org/1999/xhtml" xmlns:epub="http://www.idpf.org/2007/ops">
<head><title>t</title></head><body>{body}</body></html>"""


def make_epub(path):
    with zipfile.ZipFile(path, "w") as z:
        z.writestr("mimetype", "application/epub+zip", compress_type=zipfile.ZIP_STORED)
        z.writestr("META-INF/container.xml", """<?xml version="1.0"?>
<container version="1.0" xmlns="urn:oasis:names:tc:opendocument:xmlns:container">
<rootfiles><rootfile full-path="OEBPS/content.opf" media-type="application/oebps-package+xml"/></rootfiles>
</container>""")
        z.writestr("OEBPS/content.opf", """<?xml version="1.0" encoding="utf-8"?>
<package xmlns="http://www.idpf.org/2007/opf" version="3.0">
<metadata xmlns:dc="http://purl.org/dc/elements/1.1/">
<dc:title>Тестовый EPUB</dc:title><dc:creator>Автор Тестов</dc:creator>
<meta name="cover" content="cover"/></metadata>
<manifest>
<item id="cover" href="img/cover.jpg" media-type="image/jpeg" properties="cover-image"/>
<item id="c0" href="cover.xhtml" media-type="application/xhtml+xml"/>
<item id="c1" href="ch1.xhtml" media-type="application/xhtml+xml"/>
<item id="c2" href="ch2.xhtml" media-type="application/xhtml+xml"/>
<item id="chart" href="img/chart.png" media-type="image/png"/>
</manifest>
<spine><itemref idref="c0"/><itemref idref="c1"/><itemref idref="c2"/></spine>
</package>""")
        z.writestr("OEBPS/cover.xhtml", xhtml('<div><img src="img/cover.jpg" alt="cover"/></div>'))
        z.writestr("OEBPS/ch1.xhtml", xhtml(CH1))
        z.writestr("OEBPS/ch2.xhtml", xhtml(CH2))
        z.write(os.path.join(IMG, "cover.jpg"), "OEBPS/img/cover.jpg")
        z.write(os.path.join(IMG, "chart.png"), "OEBPS/img/chart.png")


def make_fb2(path):
    chart = base64.b64encode(open(os.path.join(IMG, "chart.png"), "rb").read()).decode()
    cover = base64.b64encode(open(os.path.join(IMG, "cover.jpg"), "rb").read()).decode()
    open(path, "w", encoding="utf-8").write(f"""<?xml version="1.0" encoding="utf-8"?>
<FictionBook xmlns="http://www.gribuser.ru/xml/fictionbook/2.0" xmlns:l="http://www.w3.org/1999/xlink">
<description><title-info><genre>poetry</genre>
<author><first-name>Тест</first-name><last-name>Поэтов</last-name></author>
<book-title>Стихи и проза</book-title>
<coverpage><image l:href="#cover.jpg"/></coverpage><lang>ru</lang></title-info></description>
<body><title><p>Стихи и проза</p></title>
<section><title><p>Часть первая</p><p>Стихи</p></title>
<epigraph><p>Эпиграф к части.</p><text-author>Неизвестный</text-author></epigraph>
<section><title><p>Глава 1</p></title>
<poem><title><p>Стихотворение</p></title>
<stanza><v>Мороз и солнце; день чудесный!</v><v>Ещё ты дремлешь, друг прелестный —</v>
<v>Пора, красавица, проснись:</v><v>Открой сомкнуты негой взоры<a l:href="#n1" type="note">[1]</a></v></stanza>
<text-author>А. С. Пушкин</text-author></poem>
<empty-line/>
<p>Обычный абзац прозы с <emphasis>выделением</emphasis> и <strong>сильным</strong> словом.</p>
<image l:href="#chart.png"/>
<subtitle>* * *</subtitle>
<table><tr><th>A</th><th>B</th><th>C</th></tr><tr><td>1</td><td>2</td><td>3</td></tr></table>
</section></section>
<section><title><p>Часть вторая</p></title><p>Проза второй части.</p>
<cite><p>Цитата в FB2.</p></cite></section>
</body>
<body name="notes"><section id="n1"><title><p>1</p></title><p>Примечание к стихотворению.</p></section></body>
<binary id="chart.png" content-type="image/png">{chart}</binary>
<binary id="cover.jpg" content-type="image/jpeg">{cover}</binary>
</FictionBook>""")


def make_html(path):
    from markdown_it import MarkdownIt
    md = MarkdownIt("commonmark", {"html": True}).enable("table")
    body = md.render(open(os.path.join(BOOKS, "rich.md"), encoding="utf-8").read())
    open(path, "w", encoding="utf-8").write(
        f"<html><head><title>HTML-книга</title></head><body>{body}</body></html>")


def main():
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    shutil.copytree(IMG, os.path.join(out, "img"), dirs_exist_ok=True)
    make_epub(os.path.join(out, "test_epub.epub"))
    make_fb2(os.path.join(out, "test_fb2.fb2"))
    make_html(os.path.join(out, "test_html.html"))


if __name__ == "__main__":
    main()
