#!/usr/bin/env python3
"""Tests for tree_diff.py:  python3 -m unittest discover -s sim/tools -p 'test_*.py'"""

import copy
import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from tree_diff import describe, plural, ELEMENTS  # noqa: E402


def node(t, x=0, y=0, w=10, h=10, children=(), **kw):
    d = {"type": t, "x": x, "y": y, "w": w, "h": h}
    d.update(kw)
    if children:
        d["children"] = list(children)
    return d


def label(text, x=0, y=0, w=60, h=20, **kw):
    return node("label", x, y, w, h, value=text, **kw)


def button(text, x=0, y=0, w=120, h=40, **kw):
    return node("btn", x, y, w, h, children=[label(text, x + 10, y + 10)], **kw)


def screen(*children):
    return {"time_ms": 1000, "screen": node("obj", 0, 0, 480, 480, children=children),
            "top": node("obj", 0, 0, 480, 480)}


def page():
    return screen(label("Timer", 10, 10, name="title"),
                  button("Start", 10, 100, name="start"),
                  button("Reset", 10, 160))


class TreeDiff(unittest.TestCase):
    def test_same_tree_says_nothing(self):
        self.assertEqual(describe(page(), page()), [])

    def test_time_alone_is_not_a_change(self):
        after = page()
        after["time_ms"] = 5000
        self.assertEqual(describe(page(), after), [])

    def test_moved(self):
        after = page()
        b = after["screen"]["children"][1]
        b["y"] += 12
        b["children"][0]["y"] += 12  # the label moves with its button: not repeated
        self.assertEqual(describe(page(), after), ["Кнопка `#start` сдвинулась на 12 px вниз"])

    def test_moved_both_ways(self):
        after = page()
        t = after["screen"]["children"][0]
        t["y"] -= 3
        t["x"] += 4
        self.assertEqual(describe(page(), after), ["Надпись `#title` сдвинулась на 3 px вверх и на 4 px вправо"])

    def test_neighbours_shifting_together_are_one_line(self):
        before = screen(label("A", 0, 0), label("B", 0, 30), label("C", 0, 60), label("D", 0, 90))
        after = screen(label("A", 0, 0, h=30), label("B", 0, 40), label("C", 0, 70), label("D", 0, 100))
        self.assertEqual(describe(before, after), [
            "Надпись «A»: высота 20 → 30",
            "На 10 px вниз сдвинулись 3 элемента: надпись «B», надпись «C» и ещё 1",
        ])

    def test_text_changed(self):
        after = page()
        after["screen"]["children"][0]["value"] = "Таймер"
        self.assertEqual(describe(page(), after), ["Надпись `#title`: «Timer» → «Таймер»"])

    def test_button_text_belongs_to_the_button(self):
        after = page()
        after["screen"]["children"][1]["children"][0]["value"] = "Stop"
        self.assertEqual(describe(page(), after), ["Кнопка `#start`: «Start» → «Stop»"])

    def test_size(self):
        after = page()
        after["screen"]["children"][2]["w"] = 140
        self.assertEqual(describe(page(), after), ["Кнопка «Reset»: ширина 120 → 140"])

    def test_children_of_a_resized_box_are_not_listed(self):
        after = page()
        b = after["screen"]["children"][2]
        b["w"] = 200
        b["children"][0]["x"] += 40  # recentred label
        self.assertEqual(describe(page(), after), ["Кнопка «Reset»: ширина 120 → 200"])

    def test_centred_text_change_is_not_a_move(self):
        before = screen(label("The Cubic", 124, 10, w=233, h=36))
        after = screen(label("Кубик", 149, 10, w=182, h=36))  # centred: x follows the width
        self.assertEqual(describe(before, after), ["Надпись: «The Cubic» → «Кубик»"])

    def test_resized_centred_button_reports_only_the_real_move(self):
        before = screen(button("Roll", 72, 336, w=336, h=60))
        after = screen(button("Roll", 48, 374, w=384, h=60))  # wider around the same centre, lower
        self.assertEqual(describe(before, after),
                         ["Кнопка «Roll» сдвинулась на 38 px вниз", "Кнопка «Roll»: ширина 336 → 384"])

    def test_grown_from_the_right_edge_is_not_a_move(self):
        before = screen(label("9", 400, 10, w=20))
        after = screen(label("10", 380, 10, w=40))  # right-aligned number
        self.assertEqual(describe(before, after), ["Надпись: «9» → «10»"])

    def test_named_element_appears_with_its_text(self):
        before = screen(label("T"))
        after = screen(label("T"), label("Нажми кнопку", 0, 100, name="hint"))
        self.assertEqual(describe(before, after), ["Появилась надпись `#hint` «Нажми кнопку»"])

    def test_inserted_element_does_not_shift_the_rest(self):
        before = screen(button("Start", 10, 100), button("Reset", 10, 160))
        after = screen(button("Lap", 10, 40), button("Start", 10, 100), button("Reset", 10, 160))
        self.assertEqual(describe(before, after), ["Появилась кнопка «Lap»"])

    def test_gone(self):
        before = screen(label("Paused", 10, 10), button("Start", 10, 100))
        after = screen(button("Start", 10, 100))
        self.assertEqual(describe(before, after), ["Пропала надпись «Paused»"])

    def test_container_appeared(self):
        before = screen(label("Title", 10, 10))
        card = node("obj", 10, 50, 300, 200, name="card",
                    children=[label("25:00", 20, 60), button("Start", 20, 100), button("Reset", 150, 100)])
        after = screen(label("Title", 10, 10), card)
        self.assertEqual(describe(before, after),
                         ["Появился блок `#card` из 5 элементов, в том числе «25:00», «Start», «Reset»"])

    def test_counts_after_iz(self):
        """«из 31 элемента», «из 32 элементов»: genitive after «из»"""
        for count, words in ((31, "из 31 элемента"), (32, "из 32 элементов")):
            card = node("obj", 0, 0, 300, 300, children=[node("led") for _ in range(count)])
            self.assertEqual(describe(screen(label("T")), screen(label("T"), card)),
                             [f"Появился блок {words}"])

    def test_verb_agrees_with_21(self):
        before = screen(node("obj", 0, 0, 480, 480))
        after = screen(node("obj", 0, 0, 480, 480, children=[node("led") for _ in range(21)]))
        self.assertEqual(describe(before, after), ["Экран был пуст — появился 21 элемент"])

    def test_blank_and_bullet_texts_are_not_quoted(self):
        before = screen(label("T"))
        lst = node("obj", 0, 30, 300, 200, children=[label("•"), label("Apple"), label("  "), label("Pear")])
        self.assertEqual(describe(before, screen(label("T"), lst)),
                         ["Появился блок из 4 элементов, в том числе «Apple», «Pear»"])

    def test_hidden_and_shown(self):
        before = screen(node("obj", 0, 0, 100, 100, name="dlg"), label("x", 0, 200, visible=False))
        after = screen(node("obj", 0, 0, 100, 100, name="dlg", visible=False), label("x", 0, 200))
        self.assertEqual(describe(before, after), ["Спрятан блок `#dlg`", "Показана надпись «x»"])

    def test_invisible_in_both_is_ignored(self):
        before = screen(label("old", 0, 0, visible=False))
        after = screen(label("new", 5, 5, visible=False))
        self.assertEqual(describe(before, after), [])

    def test_values(self):
        before = screen(node("slider", value="30", name="vol"), node("switch", value="off", name="wifi"))
        after = screen(node("slider", value="45", name="vol"), node("switch", value="on", name="wifi"))
        self.assertEqual(describe(before, after),
                         ["Ползунок `#vol`: 30 → 45", "Переключатель `#wifi`: выкл → вкл"])

    def test_stops_fitting_and_fits_again(self):
        before = screen(node("list", 0, 0, 200, 200, name="files"), node("textarea", name="note", overflow_y=5))
        after = screen(node("list", 0, 0, 200, 200, name="files", overflow_y=40), node("textarea", name="note"))
        self.assertEqual(describe(before, after), [
            "Список `#files` не помещается по высоте: +40 px",
            "Поле ввода `#note` снова помещается по высоте",
        ])

    def test_empty_screen_filled(self):
        before = screen(node("obj", 0, 0, 480, 480))
        after = screen(node("obj", 0, 0, 480, 480, children=[label("25:00"), button("Start"), button("Reset")]))
        self.assertEqual(describe(before, after),
                         ["Экран был пуст — появилось 5 элементов, в том числе «25:00», «Start», «Reset»"])

    def test_screen_emptied(self):
        self.assertEqual(describe(page(), screen()), ["Экран опустел — пропало 5 элементов"])

    def test_long_text_is_cut(self):
        after = page()
        after["screen"]["children"][0]["value"] = "Очень длинный заголовок, который не влезет в строку"
        self.assertEqual(describe(page(), after),
                         ["Надпись `#title`: «Timer» → «Очень длинный заголовок, кото…»"])

    def test_at_most_eight_lines(self):
        before = screen(*[label(f"t{i}", 0, i * 30) for i in range(20)])
        after = copy.deepcopy(before)
        for i, c in enumerate(after["screen"]["children"]):
            c["value"] = f"u{i}"
        lines = describe(before, after)
        self.assertEqual(len(lines), 8)
        self.assertEqual(lines[-1], "…и ещё 13 изменений")

    def test_top_layer_counts(self):
        before, after = page(), page()
        after["top"]["children"] = [label("Сохранено", 100, 400)]
        self.assertEqual(describe(before, after), ["Появилась надпись «Сохранено»"])

    def test_same_input_same_words(self):
        after = page()
        after["screen"]["children"][1]["y"] += 7
        self.assertEqual(describe(page(), after), describe(page(), copy.deepcopy(after)))

    def test_report_words_for_a_screen(self):
        """sim_screens: same widgets, other pixels — said so; a missing tree — no words"""
        import json
        import tempfile
        from pathlib import Path
        from sim_screens import changes, SAME_TREE
        d = Path(tempfile.mkdtemp())
        (d / "a.json").write_text(json.dumps(page()), "utf-8")
        (d / "b.json").write_text(json.dumps(page()), "utf-8")
        self.assertEqual(changes(d / "a.json", d / "b.json"), [SAME_TREE])
        self.assertIsNone(changes(d / "a.json", d / "missing.json"))

    def test_plural(self):
        self.assertEqual([plural(n, ELEMENTS) for n in (1, 2, 5, 11, 21, 22, 25, 111)],
                         ["элемент", "элемента", "элементов", "элементов", "элемент", "элемента",
                          "элементов", "элементов"])


if __name__ == "__main__":
    unittest.main()
