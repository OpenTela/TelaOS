#!/usr/bin/env python3
"""gen_pages.py — random TelaOS apps, one page each, for checking the engine on markup nobody wrote.

    python3 sim/tools/gen_pages.py <out_dir> [--count 2000] [--seed 1] [--per-batch 50]

Writes <out_dir>/batch_K/apps/gen_NNNN/gen_NNNN.bax, ready for `telaos_sim --extra-data
<out_dir>/batch_K`. Page N of a seed is always the same page, whatever the count.

A page mixes what the engine is asked to build:
  - TelaOS widgets, placed (x, y, align) and in the flow; div, table, tabs, list, select;
  - every standard HTML element the HTML layer knows (ui_html_flow.cpp), nested;
  - state bindings ({count}, {title}, an unset {nothing}), <style> with classes and ids,
    @for loops and templates (v1 <template id="Card"> and v2 <Card>);
and what it must survive: comments holding tags and '>', stray end tags, a lone '<',
<!DOCTYPE>, upper-case and over-long names, newlines inside tags, missing and implied end
tags, attributes without a value before others, entities, unknown elements.
Python standard library only.
"""

import argparse
import random
from pathlib import Path

# ---- vocabulary: the engine's own tables (test_gen_pages.py checks them against the source) ----

TELA_WIDGETS = ["label", "button", "switch", "slider", "input", "canvas", "image", "markdown", "qr",
                "select", "list", "tabs"]
TELA_CONTAINERS = ["div", "table"]

HTML_INLINE = ["a", "abbr", "acronym", "b", "bdi", "bdo", "big", "cite", "code", "data", "del", "dfn", "em",
               "font", "i", "ins", "kbd", "mark", "nobr", "output", "q", "rb", "ruby", "s", "samp", "small",
               "span", "strike", "strong", "sub", "sup", "time", "tt", "u", "var", "content", "menuitem",
               "shadow", "slot"]
HTML_RUBY_PARTS = ["rt", "rp", "rtc"]
HTML_BLOCK = ["address", "article", "aside", "blockquote", "body", "center", "fieldset", "figure", "footer",
              "form", "header", "hgroup", "main", "nav", "search", "section", "html"]
HTML_TEXT = ["h1", "h2", "h3", "h4", "h5", "h6", "p", "legend", "caption"]
HTML_LISTS = ["ul", "ol", "menu", "dir"]
HTML_DEFINITIONS = ["dl", "dt", "dd"]
HTML_SPECIAL = ["br", "wbr", "hr", "img", "picture", "details", "summary", "dialog", "figcaption", "pre", "listing",
                "xmp", "plaintext", "marquee", "meter", "progress", "textarea", "input", "style", "thead", "tbody",
                "tfoot", "th"]
HTML_HIDDEN = ["area", "base", "col", "colgroup", "datalist", "frame", "frameset", "head", "link", "map", "meta",
               "noembed", "noframes", "noscript", "optgroup", "option", "param", "script", "source", "template",
               "title", "track"]
HTML_FALLBACK = ["applet", "audio", "embed", "fencedframe", "iframe", "math", "object", "portal", "svg", "video"]

WORDS = ["Привет", "hello", "Tela", "42", "минута", "ok", "x", "Кубик", "time", "&amp;", "a&lt;b", "&copy;",
         "&nbsp;", "{count}", "{title}", "{nothing}", "длинное слово переносится", "3.14"]
COLORS = ["#ff0000", "#1c1c1c", "#4fc3f7", "#fff", "red", "teal", "#00ff0080", "{tint}"]
VOID_HTML = {"br", "wbr", "hr", "img", "area", "base", "col", "embed", "frame", "input", "link", "meta", "param",
             "source", "track"}


class Page:
    """One random page; all choices come from its own generator, so page N is stable."""

    def __init__(self, rng, index):
        self.r = rng
        self.index = index
        self.ids = 0
        self.classes = set()

    # ---- small choices ----

    def chance(self, p):
        return self.r.random() < p

    def pick(self, xs):
        return self.r.choice(xs)

    def word(self):
        return self.pick(WORDS)

    def text(self, n=3):
        return " ".join(self.word() for _ in range(self.r.randint(1, n)))

    def ident(self):
        self.ids += 1
        return f"e{self.ids}"

    def name(self, tag):
        """A tag as written: mostly lower-case, sometimes not"""
        if self.chance(0.05):
            return tag.upper()
        if self.chance(0.03):
            return tag.capitalize()
        return tag

    def gap(self):
        """Between the name and the attributes: sometimes a newline or a tab"""
        return self.pick([" ", " ", " ", " ", "\n        ", "\t"])

    def end(self, tag, missing=0.05):
        return "" if self.chance(missing) else f"</{self.name(tag)}>"

    # ---- attributes ----

    def attrs(self, kind):
        a = []
        if self.chance(0.5):
            a.append(f'id="{self.ident()}"')
        if kind == "tela":
            if self.chance(0.45):
                a.append(f'x="{self.r.randint(0, 90)}{self.pick(["%", ""])}"')
                a.append(f'y="{self.r.randint(0, 90)}{self.pick(["%", ""])}"')
            if self.chance(0.25):
                a.append(f'align="{self.pick(["center", "left", "right", "top", "bottom"])}"')
            if self.chance(0.35):
                a.append(f'w="{self.pick(["50%", "100%", "120", "80", "33%"])}"')
            if self.chance(0.25):
                a.append(f'h="{self.pick(["40", "20%", "60", "100%"])}"')
            if self.chance(0.2):
                a.append(f'font="{self.pick(["16", "24", "32", "48", "72"])}"')
            if self.chance(0.1):
                a.append(f'grow="{self.r.randint(1, 3)}"')
            if self.chance(0.05):
                a.append('visible="{show}"')
            if self.chance(0.05):
                a.append(f'onclick="tap{self.r.randint(0, 3)}"')
        if self.chance(0.2):
            a.append(f'bgcolor="{self.pick(COLORS)}"')
        if self.chance(0.2):
            a.append(f'color="{self.pick(COLORS)}"')
        if self.classes and self.chance(0.2):
            a.append(f'class="{self.pick(sorted(self.classes))}"')
        if self.chance(0.08):
            a.append(f"title='q{self.r.randint(0, 9)}'")
        if self.chance(0.05):
            a.append(f"data-n={self.r.randint(0, 99)}")  # unquoted
        if self.chance(0.04):
            a.append(f'style="color: {self.pick(COLORS[:6])}"')
        if self.chance(0.03):
            a.append('title="a>b"')  # '>' inside quotes
        self.r.shuffle(a)
        if self.chance(0.06):  # an attribute without a value, before the others
            a.insert(0, self.pick(["hidden", "disabled", "open", "checked"]))
        return self.gap() + " ".join(a) if a else ""

    # ---- TelaOS ----

    def widget(self):
        kind = self.pick(TELA_WIDGETS + ["label", "label", "button", "input_html"])
        a = self.attrs("tela")
        if kind == "input_html":
            t = self.pick(["checkbox", "radio", "range", "submit", "text", "password", "hidden", "number"])
            extra = self.pick(["", " checked", ' value="3"', ' min="0" max="10" value="7"', ' placeholder="p"'])
            return f'<input type="{t}"{a}{extra}>' if self.chance(0.8) else f'<input{extra} type="{t}"{a}>'
        if kind in ("switch", "slider", "image", "canvas"):
            if kind == "image":
                a += ' src="/apps/dice/icon.png"'
            if kind == "canvas":
                a += f' w="{self.r.randint(10, 120)}" h="{self.r.randint(10, 80)}"'
            if kind == "slider" and self.chance(0.5):
                a += f' min="0" max="100" value="{self.r.randint(0, 100)}"'
            if self.chance(0.7):
                return f"<{self.name(kind)}{a}{self.pick(['/', ' /'])}>"
            return f"<{self.name(kind)}{a}>{self.end(kind, 0.3)}"
        if kind == "select":
            opts = "".join(f"<option>{self.word()}</option>" for _ in range(self.r.randint(1, 3)))
            return f'<select{a} options="a,b,c">{opts}</select>'
        if kind == "list":
            items = "".join(f"<item>{self.word()}</item>" for _ in range(self.r.randint(1, 4)))
            return f"<list{a}>{items}</list>"
        if kind == "qr":
            return f"<qr{a}>{self.pick(['hello', 'https://tela.os', '{title}', '{nothing}', '{count}'])}</qr>"
        if kind == "markdown":
            return f"<markdown{a}># {self.word()}\n**{self.word()}** {self.text(2)}\n- {self.word()}</markdown>"
        if kind == "tabs":
            tabs = "".join(f'<tab title="{self.word()}">{"".join(self.widget() for _ in range(self.r.randint(0, 2)))}</tab>'
                           for _ in range(self.r.randint(1, 3)))
            return f'<tabs id="{self.ident()}" w="100%" h="50%">{tabs}</tabs>'
        if kind == "input":
            return f'<input{a} placeholder="{self.word()}">{self.end("input", 0.4)}'
        if self.chance(0.12):
            return f"<{self.name(kind)}{a}/>"
        return f"<{self.name(kind)}{a}>{self.text()}{self.end(kind)}"

    def container(self, depth):
        if self.chance(0.65):
            a = self.attrs("tela")
            if self.chance(0.6):
                a += f' flex="{self.pick(["row", "column", "row-wrap", "column-wrap", "row-reverse", "column-reverse"])}"'
            if self.chance(0.3):
                a += f' gap="{self.r.randint(0, 12)}"'
            if self.chance(0.25):
                a += f' justify="{self.pick(["center", "between", "end", "evenly", "around"])}"'
            if self.chance(0.25):
                a += f' align="{self.pick(["center", "end", "stretch"])}"'
            if self.chance(0.1):
                a += ' overflow="scroll"'
            if self.chance(0.1):
                a += f' radius="{self.r.randint(0, 16)}" padding="{self.r.randint(0, 12)}"'
            inner = "".join(self.item(depth + 1) for _ in range(self.r.randint(0, 4)))
            if self.chance(0.15):
                inner = self.text(2) + inner  # direct text: HTML flow inside a TelaOS container
            return f"<{self.name('div')}{a}>{inner}{self.end('div', 0.04)}"
        rows = []
        for _ in range(self.r.randint(1, 3)):
            cells = []
            for _ in range(self.r.randint(1, 3)):
                inner = (self.text(1) if self.chance(0.4)
                         else "".join(self.item(depth + 2) for _ in range(self.r.randint(0, 2))))
                cells.append(f"<td{self.attrs('tela') if self.chance(0.3) else ''}>{inner}" + self.end("td", 0.1))
            height = ' h="40"' if self.chance(0.1) else ""
            rows.append(f"<tr{height}>{''.join(cells)}" + self.end("tr", 0.1))
        extra = f' cellspacing="{self.r.randint(0, 8)}"' if self.chance(0.3) else ""
        return f"<table{self.attrs('tela')}{extra}>{''.join(rows)}</table>"

    # ---- standard HTML ----

    def inline(self, depth):
        t = self.pick(HTML_INLINE + ["br", "img", "a", "ruby", "input"])
        if t == "br":
            return self.pick(["<br>", "<br/>", "<BR>", "<br />", "<wbr>"])
        if t == "img":
            return f'<img src="/apps/dice/icon.png" alt="{self.word()}"{self.pick(["", " width=40 height=30"])}>'
        if t == "input":
            return f'<input type="{self.pick(["checkbox", "radio", "range"])}">'
        if t == "a":
            return f'<a href="{self.pick(["/main", "#e1", "/nowhere"])}">{self.text(2)}</a>'
        if t == "ruby":
            rt = self.pick(HTML_RUBY_PARTS[:1] + HTML_RUBY_PARTS[2:])  # rt or rtc
            return f"<ruby>{self.word()}<rp>(</rp><{rt}>{self.word()}</{rt}><rp>)</rp></ruby>"
        if t == "font":
            return f'<font color="{self.pick(COLORS[:6])}" size="{self.r.randint(1, 7)}">{self.word()}</font>'
        inner = self.text(2) if depth > 2 or self.chance(0.6) else self.inline(depth + 1)
        return f"<{self.name(t)}{self.attrs('html') if self.chance(0.15) else ''}>{inner}{self.end(t, 0.08)}"

    def paragraph(self, depth):
        parts = [self.text(3)]
        for _ in range(self.r.randint(0, 3)):
            parts.append(self.inline(depth) if self.chance(0.7) else self.text(2))
        return " ".join(parts)

    def block(self, depth):
        t = self.pick(HTML_BLOCK + HTML_TEXT + HTML_LISTS + HTML_SPECIAL + HTML_FALLBACK + HTML_HIDDEN
                      + ["dl", "table_html", "my-widget", "p", "p", "ul"])
        if t in ("br", "wbr", "hr"):
            return self.pick([f"<{t}>", f"<{t}/>"])
        if t in HTML_LISTS:
            items = []
            for _ in range(self.r.randint(1, 4)):
                inner = self.paragraph(depth) if depth > 2 or self.chance(0.7) else self.block(depth + 1)
                items.append(f"<li>{inner}" + ("" if self.chance(0.4) else "</li>"))  # omitted ends
            return f"<{self.name(t)}>{''.join(items)}</{t}>"
        if t in ("dl", "dt", "dd"):
            parts = []
            for _ in range(self.r.randint(1, 3)):
                parts.append(f"<dt>{self.word()}" + ("" if self.chance(0.5) else "</dt>"))
                parts.append(f"<dd>{self.text(2)}" + ("" if self.chance(0.5) else "</dd>"))
            return f"<dl>{''.join(parts)}</dl>"
        if t in ("details", "summary"):
            return (f"<details{' open' if self.chance(0.5) else ''}><summary>{self.word()}</summary>"
                    f"{self.paragraph(depth)}</details>")
        if t == "dialog":
            return f"<dialog{' open' if self.chance(0.4) else ''}>{self.paragraph(depth)}</dialog>"
        if t in ("figure", "figcaption"):
            return f'<figure><img src="/apps/dice/icon.png"><figcaption>{self.word()}</figcaption></figure>'
        if t in ("picture",):
            return '<picture><source srcset="x.webp"><img src="/apps/dice/icon.png"></picture>'
        if t in ("pre", "listing", "xmp", "plaintext"):
            return f"<{t}>{self.text(2)}\n  <b>{self.word()}</b> {self.text(2)}</{t}>"
        if t in ("textarea",):
            return f'<textarea rows="{self.r.randint(1, 4)}">{self.text(2)} <b>not bold</b></textarea>'
        if t == "marquee":
            return f"<marquee>{self.text(2)}</marquee>"
        if t == "progress":
            return f'<progress value="{self.r.randint(0, 10)}" max="10"></progress>'
        if t == "meter":
            return f'<meter min="0" max="10" low="3" high="7" value="{self.r.randint(0, 10)}"></meter>'
        if t == "input":
            return f'<input type="{self.pick(["text", "checkbox", "submit", "image", "reset"])}" value="{self.word()}">'
        if t == "style":
            return f"<style>.{self.css_class()} {{ color: {self.pick(COLORS[:6])}; }}</style>"
        if t in ("thead", "tbody", "tfoot", "th", "caption", "table_html"):
            rows = []
            for _ in range(self.r.randint(1, 3)):
                cells = "".join(f"<td>{self.word()}" + ("" if self.chance(0.3) else "</td>")
                                for _ in range(self.r.randint(1, 3)))
                rows.append(f"<tr>{cells}" + ("" if self.chance(0.3) else "</tr>"))
            head = f"<thead><tr><th>{self.word()}</th></tr></thead>" if self.chance(0.4) else ""
            cap = f"<caption>{self.word()}</caption>" if self.chance(0.3) else ""
            foot = f"<tfoot><tr><td>{self.word()}</td></tr></tfoot>" if self.chance(0.2) else ""
            return f"<table>{cap}{head}<tbody>{''.join(rows)}</tbody>{foot}</table>"
        if t in HTML_FALLBACK:
            fallback = self.text(2) if self.chance(0.6) else ""
            return f'<{t} src="x">' + ("" if t in VOID_HTML else f"{fallback}</{t}>")
        if t in HTML_HIDDEN:
            if t in VOID_HTML:
                return f"<{t}>"
            body = "local hidden = 1" if t == "script" else self.text(2)
            return f"<{t}>{body}</{t}>"
        if t in HTML_BLOCK or t == "my-widget":
            inner = "".join(self.flow_item(depth + 1) for _ in range(self.r.randint(1, 3)))
            return f"<{self.name(t)}{self.attrs('html') if self.chance(0.3) else ''}>{inner}{self.end(t, 0.06)}"
        end = "" if (t == "p" and self.chance(0.3)) else f"</{t}>"
        return f"<{self.name(t)}{self.attrs('html') if self.chance(0.2) else ''}>{self.paragraph(depth)}{end}"

    # ---- what the walker must survive ----

    def noise(self):
        return self.pick([
            "<!-- a comment -->",
            "<!-- <label>hidden</label> > still a comment -->",
            "</div>", "</p>", "</label>", "</li>",
            "<!DOCTYPE html>",
            " a < b ",
            "< ",
            f"<{'x' * self.r.randint(32, 40)} id=\"long\">{self.word()}</{'x' * 31}>",
            f'<tab title="lost">{self.word()}</tab>',
            "<unknown-thing>?</unknown-thing>",
            "&nbsp;&copy;&#8364;&#x41;",
        ])

    # ---- page ----

    def flow_item(self, depth):
        x = self.r.random()
        if x < 0.45 and depth < 4:
            return self.block(depth)
        if x < 0.65:
            return self.paragraph(depth)
        if x < 0.8:
            return self.widget()
        if x < 0.9 and depth < 4:
            return self.container(depth)
        return self.noise()

    def item(self, depth):
        x = self.r.random()
        if x < 0.45:
            return self.widget()
        if x < 0.65 and depth < 4:
            return self.container(depth)
        if x < 0.8 and depth < 4:
            return self.block(depth)
        if x < 0.92:
            return self.noise()
        return self.paragraph(depth)

    def css_class(self):
        name = f"c{self.r.randint(0, 4)}"
        self.classes.add(name)
        return name

    def body(self):
        style = self.pick(["tela", "tela", "html", "mix", "mix"])
        for _ in range(self.r.randint(0, 3)):
            self.css_class()
        n = self.r.randint(2, 9)
        if style == "tela":
            parts = [self.item(1) for _ in range(n)]
        elif style == "html":
            parts = [self.flow_item(1) for _ in range(n)]
        else:
            parts = [self.pick([self.item, self.flow_item])(1) for _ in range(n)]
        return self.pick(["\n      ", "", " "]).join(parts)

    def app(self):
        v2 = self.chance(0.25)
        body = self.body()
        templates = ""
        if self.chance(0.2):  # @for: the page is walked from an expanded copy
            body += (f'\n      @for(n in 0..{self.r.randint(0, 3)}) {{ <label id="for_{{n}}">{{n}}</label>'
                     f'{self.widget()} }}')
        if self.chance(0.15):  # a template call: an expanded copy too
            if v2:
                templates = ('<templates><Card><div flex="column"><label>{title}</label>'
                             '<button id="cb_{title}">go</button></div></Card></templates>')
            else:
                templates = ('<templates><template id="Card"><div flex="column"><label>{title}</label>'
                             '<button id="cb_{title}">go</button></div></template></templates>')
            body = f'<Card title="t{self.index}"/>' + body + f'<Card title="u{self.index}"></Card>'
        css = ""
        if self.classes and self.chance(0.6):
            rules = [f".{c} {{ color: {self.pick(COLORS[:6])}; background: {self.pick(COLORS[:6])}; }}"
                     for c in sorted(self.classes)]
            if self.chance(0.3):
                rules.append(f"#e1 {{ font-size: {self.pick(['16px', '32px', '48px'])}; }}")
            css = "\n  <style>\n    " + "\n    ".join(rules) + "\n  </style>"
        if v2:
            state = "<state>\n    count: 3\n    title: T\n    show: true\n    tint: teal\n  </state>"
        else:
            state = ('<state>\n    <int name="count" default="3"/>\n    <string name="title" default="T"/>\n'
                     '    <bool name="show" default="true"/>\n    <string name="tint" default="#4fc3f7"/>\n  </state>')
        return (f'<app title="gen {self.index}" version="1.0" os="{"2.0" if v2 else "1.0"}" category="test">\n'
                f'  <ui default="/main">\n    <page id="main">\n      {body}\n    </page>\n  </ui>\n'
                f"  {templates}\n  {state}{css}\n</app>\n")


def page_name(index):
    return f"gen_{index:04d}"


def page(seed, index):
    """The text of page `index` for `seed`"""
    return Page(random.Random(f"{seed}/{index}"), index).app()


def write(out_dir, count, seed=1, per_batch=50):
    """Writes the apps in batches; returns the batch folders (each one an --extra-data root)"""
    out = Path(out_dir)
    batches = []
    for i in range(count):
        batch = out / f"batch_{i // per_batch}"
        if not batches or batches[-1] != batch:
            batches.append(batch)
        d = batch / "apps" / page_name(i)
        d.mkdir(parents=True, exist_ok=True)
        (d / f"{page_name(i)}.bax").write_text(page(seed, i), encoding="utf-8")
    return batches


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("out")
    ap.add_argument("--count", type=int, default=2000)
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--per-batch", type=int, default=50)
    args = ap.parse_args()
    batches = write(args.out, args.count, args.seed, args.per_batch)
    print(f"{args.count} pages (seed {args.seed}) in {len(batches)} batches under {args.out}")


if __name__ == "__main__":
    main()
