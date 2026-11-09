#!/usr/bin/env python3
"""Tests for sim_wasm.py (no Emscripten, no simulator: the shots and trees are written here):
python3 -m unittest discover -s sim/tools -p 'test_*.py'"""

import json
import os
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sim_screens  # noqa: E402
import sim_wasm  # noqa: E402

# A tree as the robot writes it, cut down: the heaps are 64-bit on the PC, 32-bit in WebAssembly
TREE = {"screen": "main", "mem": {"dram": {"free": 134232, "blocks": 218}},
        "widgets": [{"id": "title", "text": "Hello", "x": 0, "y": 10, "w": 480, "h": 36}],
        "dom": [{"tag": "p", "mem": "a field of the page, not the heap"}]}


def tree_with(**changes):
    t = json.loads(json.dumps(TREE))
    for k, v in changes.items():
        t[k] = v
    return t


class WithoutMem(unittest.TestCase):
    def test_takes_every_mem_out(self):
        self.assertEqual(sim_wasm.without_mem(TREE),
                         {"screen": "main", "widgets": TREE["widgets"], "dom": [{"tag": "p"}]})

    def test_other_values_stay(self):
        self.assertEqual(sim_wasm.without_mem([1, {"a": [2, {"mem": 3, "b": 4}]}]), [1, {"a": [2, {"b": 4}]}])


class Compare(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        root = Path(self.tmp.name)
        self.pc, self.wasm = root / "pc", root / "wasm"
        for d in (self.pc, self.wasm):
            (d / "tela_hello").mkdir(parents=True)

    def tearDown(self):
        self.tmp.cleanup()

    def png(self, side, name, rgb=(0, 0, 0)):
        sim_screens.write_png(side / "tela_hello" / name, 2, 1, bytes(rgb) * 2)

    def tree(self, side, name, tree):
        (side / "tela_hello" / name).write_text(json.dumps(tree))

    def test_same_pixels_and_trees_but_the_heap(self):
        self.png(self.pc, "a.png")
        self.png(self.wasm, "a.png")
        self.tree(self.pc, "t.json", TREE)
        self.tree(self.wasm, "t.json", tree_with(mem={"dram": {"free": 139268, "blocks": 217}}))
        found = sim_wasm.compare(self.pc, self.wasm)
        self.assertEqual(found, {"same": ["tela_hello/a.png", "tela_hello/t.json"], "different": [], "missing": []})

    def test_one_pixel_differs(self):
        self.png(self.pc, "a.png", (0, 0, 0))
        self.png(self.wasm, "a.png", (0, 0, 1))
        self.assertEqual(sim_wasm.compare(self.pc, self.wasm)["different"], ["tela_hello/a.png"])

    def test_a_widget_moved(self):
        self.tree(self.pc, "t.json", TREE)
        moved = tree_with(widgets=[dict(TREE["widgets"][0], y=11)])
        self.tree(self.wasm, "t.json", moved)
        self.assertEqual(sim_wasm.compare(self.pc, self.wasm)["different"], ["tela_hello/t.json"])

    def test_missing_and_extra_shots(self):
        self.png(self.pc, "a.png")
        self.png(self.wasm, "b.png")
        found = sim_wasm.compare(self.pc, self.wasm)
        self.assertEqual(found["missing"], ["tela_hello/a.png"])
        self.assertEqual(found["different"], ["tela_hello/b.png (only in WebAssembly)"])


class Report(unittest.TestCase):
    def robots(self, *rows):
        return [{"name": n, "passed": p, "failed": f, "ok": f == 0} for n, p, f in rows]

    def test_all_the_same(self):
        found = {"same": ["x/a.png", "x/b.png", "x/t.json"], "different": [], "missing": []}
        page = [("telaos_sim.html", 6641), ("telaos_sim.wasm", 3037382)]
        text = "\n".join(sim_wasm.report(self.robots(("tela_hello", 7, 0)), self.robots(("tela_hello", 7, 0)),
                                         found, page))
        self.assertIn("Роботы на ПК: 7/7; в WebAssembly (Node.js): 7/7.", text)
        self.assertIn("снимков 2, деревьев 1", text)
        self.assertIn("`telaos_sim.html` 6 КБ, `telaos_sim.wasm` 2,9 МБ, всего 2,9 МБ", text)
        self.assertNotIn("Отличаются", text)

    def test_a_failed_robot_and_a_difference_are_named(self):
        found = {"same": [], "different": ["tela_css/css_01.png"], "missing": ["tela_css/t.json"]}
        text = "\n".join(sim_wasm.report(self.robots(("tela_css", 45, 0)), self.robots(("tela_css", 44, 1)),
                                         found, []))
        self.assertIn("в WebAssembly (Node.js): 44/45, не прошли: tela_css", text)
        self.assertIn("**Отличаются:** tela_css/css_01.png", text)
        self.assertIn("**Нет в WebAssembly:** tela_css/t.json", text)


class Runner(unittest.TestCase):
    def test_runs_node_with_the_arguments(self):
        with tempfile.TemporaryDirectory() as tmp:
            js = Path(tmp) / "telaos_sim.js"
            js.write_text("")
            runner = sim_wasm.node_runner(js, {"EMSDK_NODE": "/opt/node/bin/node"})
            self.assertEqual(runner.name, "telaos_sim")
            self.assertTrue(os.access(runner, os.X_OK))
            self.assertIn(f'exec "/opt/node/bin/node" "{js.resolve()}" "$@"', runner.read_text())


if __name__ == "__main__":
    unittest.main()
