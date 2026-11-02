#!/usr/bin/env python3
"""tree_diff.py — what changed on a screen, in words.

    python3 sim/tools/tree_diff.py before.json after.json

Compares two widget trees written by the robot's `tree` command (type, id, box, visibility,
value, overflow) and says in plain Russian what a person would notice: elements that
appeared or disappeared, moved, changed size, text or value, were shown or hidden, stopped
fitting. An element that moves together with its parent is not repeated, and neighbours that
shift by the same amount make one line. The same two trees always give the same lines.
Python standard library only.
"""

import difflib
import json
import sys

MAX_LINES = 8   # per screen; the rest are counted
QUOTE = 30      # longest quoted text, characters

# LVGL widget class -> (word, grammatical gender: 0 he, 1 she, 2 it, 3 they)
KINDS = {
    "obj": ("блок", 0),
    "label": ("надпись", 1),
    "span": ("текст", 0),
    "spangroup": ("текст", 0),
    "btn": ("кнопка", 1),
    "imagebutton": ("кнопка", 1),
    "textarea": ("поле ввода", 2),
    "slider": ("ползунок", 0),
    "switch": ("переключатель", 0),
    "checkbox": ("флажок", 0),
    "dropdown": ("выпадающий список", 0),
    "dropdown-list": ("выпадающий список", 0),
    "roller": ("барабан", 0),
    "roller-label": ("надпись", 1),
    "bar": ("полоса", 1),
    "arc": ("дуга", 1),
    "image": ("картинка", 1),
    "animimg": ("анимация", 1),
    "gif": ("анимация", 1),
    "list": ("список", 0),
    "list-btn": ("пункт списка", 0),
    "list-text": ("подпись списка", 1),
    "tileview": ("листалка", 1),
    "tile": ("страница", 1),
    "keyboard": ("клавиатура", 1),
    "btnmatrix": ("кнопки", 3),
    "table": ("таблица", 1),
    "chart": ("график", 0),
    "led": ("светодиод", 0),
    "line": ("линия", 1),
    "canvas": ("холст", 0),
    "msgbox": ("окно сообщения", 2),
    "spinner": ("индикатор загрузки", 0),
    "spinbox": ("числовое поле", 2),
    "tabview": ("вкладки", 3),
    "menu": ("меню", 2),
    "calendar": ("календарь", 0),
    "scale": ("шкала", 1),
    "qrcode": ("QR-код", 0),
    "barcode": ("штрихкод", 0),
    "win": ("окно", 2),
}
VERBS = {
    "moved": ("сдвинулся", "сдвинулась", "сдвинулось", "сдвинулись"),
    "new": ("Появился", "Появилась", "Появилось", "Появились"),
    "gone": ("Пропал", "Пропала", "Пропало", "Пропали"),
    "shown": ("Показан", "Показана", "Показано", "Показаны"),
    "hidden": ("Спрятан", "Спрятана", "Спрятано", "Спрятаны"),
    "nofit": ("не помещается",) * 3 + ("не помещаются",),
    "fits": ("снова помещается",) * 3 + ("снова помещаются",),
}
TEXT_TYPES = {"label", "span", "spangroup", "textarea", "roller-label", "list-text"}
LABELLED = {"btn", "imagebutton", "list-btn", "checkbox"}  # their label's text is theirs
CONTAINERS = {"obj", "list", "tile", "tileview", "tabview", "menu", "win", "msgbox"}
ELEMENTS = ("элемент", "элемента", "элементов")
ELEMENTS_OF = ("элемента", "элементов", "элементов")  # after «из»: из 31 элемента, из 32 элементов
CHANGES = ("изменение", "изменения", "изменений")


def plural(n, forms):
    n = abs(n) % 100
    if 11 <= n <= 14:
        return forms[2]
    n %= 10
    return forms[0] if n == 1 else forms[1] if 2 <= n <= 4 else forms[2]


def agree(n, one, many):
    """The verb before a count: «появился 21 элемент», «появилось 5 элементов»."""
    return plural(n, (one, many, many))


def readable(s):
    return any(ch.isalnum() for ch in s)  # not a bullet, a dash or blank


def quote(s):
    s = " ".join(s.split())
    if not s:
        return "пусто"
    return f"«{s if len(s) <= QUOTE else s[:QUOTE - 1] + '…'}»"


def capital(s):
    return s[:1].upper() + s[1:]


class Node:
    __slots__ = ("type", "name", "x", "y", "w", "h", "visible", "value", "ovx", "ovy",
                 "parent", "children", "_text")

    def __init__(self, d, parent=None):
        self.type = d.get("type", "obj")
        self.name = d.get("name", "")
        self.x, self.y, self.w, self.h = (d.get(k, 0) for k in ("x", "y", "w", "h"))
        self.visible = d.get("visible", True)
        self.value = d.get("value", "")
        self.ovx, self.ovy = d.get("overflow_x", 0), d.get("overflow_y", 0)
        self.parent = parent
        self.children = [Node(c, self) for c in d.get("children", [])]
        self._text = None

    def texts(self):
        """Visible text pieces of this subtree, in order (what a person reads)."""
        if not self.visible:
            return []
        out = [self.value] if self.type in TEXT_TYPES and readable(self.value) else []
        for c in self.children:
            out += c.texts()
        return out

    def text(self):
        if self._text is None:
            self._text = " ".join(" ".join(self.texts()).split())
        return self._text

    def content(self):
        """Visible nodes a person would count: everything but bare unnamed containers."""
        if not self.visible:
            return []
        out = [self] if self.type != "obj" or self.value or self.name else []
        for c in self.children:
            out += c.content()
        return out


def kind(node):
    return KINDS.get(node.type, (node.type, 0))


def subject(node, with_text=True):
    word, _ = kind(node)
    if node.name:
        return f"{word} `#{node.name}`"
    if with_text and node.text():
        return f"{word} {quote(node.text())}"
    return word if not with_text else f"{word} {node.w}×{node.h}"


def show(node, v):
    if node.type in TEXT_TYPES:
        return quote(v)  # text, even when it is a number
    if v in ("on", "off"):
        return "вкл" if v == "on" else "выкл"
    try:
        float(v)
        return v
    except ValueError:
        return quote(v)


def shift(dx, dy):
    parts = []
    if dy:
        parts.append(f"на {abs(dy)} px {'вниз' if dy > 0 else 'вверх'}")
    if dx:
        parts.append(f"на {abs(dx)} px {'вправо' if dx > 0 else 'влево'}")
    return " и ".join(parts)


def including(texts):
    """«…, в том числе «A», «B», «C»»: a few of the texts a person would read first."""
    return f", в том числе {', '.join(quote(t) for t in texts[:3])}" if texts else ""


# ============ Matching two trees ============

def sig(n):
    return n.type, n.name, n.value or n.text()[:60]


def align(xs, ys):
    """Pairs children of two matched parents: same signature first (an inserted element does
    not shift the others), then by type and id inside each changed stretch."""
    sm = difflib.SequenceMatcher(None, [sig(x) for x in xs], [sig(y) for y in ys], autojunk=False)
    for op, i1, i2, j1, j2 in sm.get_opcodes():
        if op == "equal":
            for x, y in zip(xs[i1:i2], ys[j1:j2]):
                yield "pair", x, y
            continue
        ys_part, used = ys[j1:j2], [False] * (j2 - j1)
        for x in xs[i1:i2]:
            k = next((k for k, y in enumerate(ys_part)
                      if not used[k] and y.type == x.type and y.name == x.name), None)
            if k is None:
                yield "gone", x, None
            else:
                used[k] = True
                yield "pair", x, ys_part[k]
        for k, y in enumerate(ys_part):
            if not used[k]:
                yield "new", None, y


def offset(a0, a_len, b0, b_len):
    """How far an element moved along one axis, not counting what its new size explains:
    if its start, centre (±1 px of rounding) or end stayed put, it did not move."""
    if a_len != b_len and (a0 == b0 or a0 + a_len == b0 + b_len
                           or abs((2 * a0 + a_len) - (2 * b0 + b_len)) <= 2):
        return 0
    return b0 - a0


def walk(a, b, pdx, pdy, parent_resized, events):
    if not a.visible and not b.visible:
        return  # nothing a person sees
    if a.visible != b.visible:
        events.append(("shown" if b.visible else "hidden", b))
        return
    dx, dy = b.x - a.x, b.y - a.y
    mx, my = offset(a.x, a.w, b.x - pdx, b.w), offset(a.y, a.h, b.y - pdy, b.h)
    if (mx or my) and not parent_resized:
        events.append(("moved", b, mx, my))
    resized = (a.w, a.h) != (b.w, b.h)
    if a.value != b.value:
        events.append(("value", b, a.value, b.value))  # new text: new size, nothing to add
    elif resized:
        events.append(("size", b, a.w, a.h))
    for axis in ("y", "x"):
        was, now = getattr(a, "ov" + axis), getattr(b, "ov" + axis)
        if was != now:
            events.append(("fit", b, axis, was, now))
    for k, x, y in align(a.children, b.children):
        if k == "pair":
            walk(x, y, dx, dy, resized, events)
        elif k == "gone" and x.visible:
            events.append(("gone", x))
        elif k == "new" and y.visible:
            events.append(("new", y))


# ============ Words ============

def line(e):
    k, n = e[0], e[1]
    word, g = kind(n)
    if k in ("new", "gone"):
        inner = [c for c in n.content() if c is not n]
        if n.type in CONTAINERS and len(inner) >= 2:
            what = f"{word} `#{n.name}`" if n.name else word
            return (f"{VERBS[k][g]} {what} из {len(inner)} {plural(len(inner), ELEMENTS_OF)}"
                    + including(n.texts()))
        if n.name and n.text():  # what appeared is its text as much as its id
            return f"{VERBS[k][g]} {subject(n)} {quote(n.text())}"
        return f"{VERBS[k][g]} {subject(n)}"
    if k in ("shown", "hidden"):
        return f"{VERBS[k][g]} {subject(n)}"
    if k == "value":
        owner = n.parent if n.type == "label" and n.parent and n.parent.type in LABELLED else n
        return f"{capital(subject(owner, with_text=False))}: {show(n, e[2])} → {show(n, e[3])}"
    if k == "size":
        w0, h0 = e[2], e[3]
        if h0 == n.h:
            change = f"ширина {w0} → {n.w}"
        elif w0 == n.w:
            change = f"высота {h0} → {n.h}"
        else:
            change = f"размер {w0}×{h0} → {n.w}×{n.h}"
        return f"{capital(subject(n))}: {change}"
    if k == "fit":
        axis, was, now = e[2], e[3], e[4]
        where = "по высоте" if axis == "y" else "по ширине"
        if now:
            return (f"{capital(subject(n))} {VERBS['nofit'][g]} {where}: +{now} px"
                    + (f" (было +{was})" if was else ""))
        return f"{capital(subject(n))} {VERBS['fits'][g]} {where}"
    raise ValueError(k)


def moved_line(group):
    n, dx, dy = group[0][1], group[0][2], group[0][3]
    if len(group) == 1:
        return f"{capital(subject(n))} {VERBS['moved'][kind(n)[1]]} {shift(dx, dy)}"
    names = [subject(e[1]) for e in group]
    shown = ", ".join(names[:2]) + (f" и ещё {len(names) - 2}" if len(names) > 2 else "")
    n = len(group)
    return (f"{capital(shift(dx, dy))} {agree(n, 'сдвинулся', 'сдвинулись')} {n} "
            f"{plural(n, ELEMENTS)}: {shown}")


def render(events):
    lines, i = [], 0
    while i < len(events):
        e = events[i]
        if e[0] != "moved":
            lines.append(line(e))
            i += 1
            continue
        j = i + 1
        while (j < len(events) and events[j][0] == "moved" and events[j][1].parent is e[1].parent
               and events[j][2:] == e[2:]):
            j += 1
        lines.append(moved_line(events[i:j]))
        i = j
    return lines


def describe(before, after, max_lines=MAX_LINES):
    """Lines in Russian: what changed between two `tree` dumps (parsed JSON)."""
    lines = []
    for layer in ("screen", "top"):
        if layer not in before or layer not in after:
            continue
        a, b = Node(before[layer]), Node(after[layer])
        if layer == "screen":
            had, has = a.content(), b.content()
            if not had and has:
                n = len(has)
                lines.append(f"Экран был пуст — {agree(n, 'появился', 'появилось')} {n} "
                             f"{plural(n, ELEMENTS)}{including(b.texts())}")
                continue
            if had and not has:
                n = len(had)
                lines.append(f"Экран опустел — {agree(n, 'пропал', 'пропало')} {n} {plural(n, ELEMENTS)}")
                continue
        events = []
        walk(a, b, 0, 0, False, events)
        lines += render(events)
    if len(lines) > max_lines:
        rest = len(lines) - (max_lines - 1)
        lines = lines[:max_lines - 1] + [f"…и ещё {rest} {plural(rest, CHANGES)}"]
    return lines


def main(argv):
    if len(argv) != 3:
        print(__doc__.strip().splitlines()[2].strip())
        return 2
    with open(argv[1], encoding="utf-8") as f:
        before = json.load(f)
    with open(argv[2], encoding="utf-8") as f:
        after = json.load(f)
    for text in describe(before, after) or ["Дерево виджетов то же."]:
        print(f"- {text}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
