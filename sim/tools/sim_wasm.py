#!/usr/bin/env python3
"""sim_wasm.py — the simulator as WebAssembly against the simulator on the PC: the same robots,
the same pixels, and the page that opens in a browser.

    python3 sim/tools/sim_wasm.py --head . --out build/wasm [--emsdk ~/emsdk]

Builds sim/ of --head three times: for the PC (as sim_screens.py does), as WebAssembly for
Node.js (-DTELA_SIM_WASM=node) and as the page (sim/sim_wasm.cmake). Runs every robot test
(sim/tests/*.sim) on the first two and compares what they leave: every screenshot pixel by pixel,
every tree apart from "mem" — WebAssembly is a 32-bit build, the PC a 64-bit one, so their heaps
hold different numbers of bytes. Writes:

    out/wasm.md       the robots on both, the screens and trees, the size of the page
    out/page/         the page: telaos_sim.html, .js, .wasm, .data, for any web server
    out/logs/         the logs

Exit code 1 if a build fails, a robot test fails on either, or a screen or a tree differs.
Emscripten is taken from --emsdk (an emsdk folder with an active SDK) or from PATH (emcmake).
Python standard library only.
"""

import argparse
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

import sim_screens


def emscripten_env(emsdk):
    """The environment with Emscripten in it: emsdk_env.sh of --emsdk, or this one if emcmake
    is on PATH already; None if there is none"""
    if not emsdk:
        return dict(os.environ) if shutil.which("emcmake") else None
    script = Path(emsdk) / "emsdk_env.sh"
    if not script.exists():
        return None
    p = subprocess.run(["bash", "-c", f'source "{script}" >/dev/null 2>&1 && env -0'],
                       stdout=subprocess.PIPE, check=False)
    env = dict(item.split("=", 1) for item in p.stdout.decode().split("\0") if "=" in item)
    return env if shutil.which("emcmake", path=env.get("PATH")) else None


def build_wasm(sim_dir, telaos_dir, build_dir, log, variant, env):
    """emcmake + cmake --build; the built .js (node) or .html (web), or None"""
    cfg = ["emcmake", "cmake", "-S", str(sim_dir), "-B", str(build_dir), f"-DTELAOS_DIR={telaos_dir}",
           f"-DTELA_SIM_WASM={variant}"]
    if shutil.which("ninja", path=env.get("PATH")):
        cfg += ["-G", "Ninja"]
    with open(log, "w") as f:
        for cmd in (cfg, ["cmake", "--build", str(build_dir), "-j", str(os.cpu_count() or 2)]):
            f.write("$ " + " ".join(cmd) + "\n")
            f.flush()
            if subprocess.run(cmd, stdout=f, stderr=subprocess.STDOUT, env=env).returncode != 0:
                return None
    out = Path(build_dir) / ("telaos_sim.js" if variant == "node" else "telaos_sim.html")
    return out if out.exists() else None


def node_runner(js, env):
    """A telaos_sim next to telaos_sim.js that runs it under Node.js: sim_screens.run_tests
    takes it as it takes the PC binary"""
    node = env.get("EMSDK_NODE") or shutil.which("node", path=env.get("PATH")) or "node"
    runner = Path(js).with_name("telaos_sim")
    runner.write_text(f'#!/bin/sh\nexec "{node}" "{Path(js).resolve()}" "$@"\n')
    runner.chmod(0o755)
    return runner


def without_mem(tree):
    """The tree with every "mem" taken out: the heaps' numbers depend on the pointer size"""
    if isinstance(tree, dict):
        return {k: without_mem(v) for k, v in tree.items() if k != "mem"}
    if isinstance(tree, list):
        return [without_mem(v) for v in tree]
    return tree


def compare(pc_dir, wasm_dir):
    """What the robots left on the PC against WebAssembly: {"same", "different", "missing"},
    each a list of paths relative to pc_dir. Pictures by pixels, trees without "mem"."""
    result = {"same": [], "different": [], "missing": []}
    pc_dir, wasm_dir = Path(pc_dir), Path(wasm_dir)
    for a in sorted(list(pc_dir.rglob("*.png")) + list(pc_dir.rglob("*.json"))):
        rel = a.relative_to(pc_dir).as_posix()
        b = wasm_dir / rel
        if not b.exists():
            result["missing"].append(rel)
            continue
        if a.suffix == ".png":
            same = sim_screens.read_png(a) == sim_screens.read_png(b)
        else:
            same = without_mem(json.loads(a.read_text())) == without_mem(json.loads(b.read_text()))
        result["same" if same else "different"].append(rel)
    for b in sorted(list(wasm_dir.rglob("*.png")) + list(wasm_dir.rglob("*.json"))):
        if not (pc_dir / b.relative_to(wasm_dir)).exists():
            result["different"].append(b.relative_to(wasm_dir).as_posix() + " (only in WebAssembly)")
    return result


def kb(n):
    return f"{n / 1024:,.0f} КБ".replace(",", " ")


def mb(n):
    return f"{n / 1048576:.1f} МБ".replace(".", ",")


def report(pc_tests, wasm_tests, found, page):
    """The lines of wasm.md"""
    def robots(tests):
        passed = sum(t["passed"] for t in tests)
        failed = sum(t["failed"] for t in tests)
        bad = [t["name"] for t in tests if not t["ok"]]
        return f"{passed}/{passed + failed}" + (f", не прошли: {', '.join(bad)}" if bad else "")

    pictures = [p for p in found["same"] if p.endswith(".png")]
    trees = [p for p in found["same"] if p.endswith(".json")]
    lines = ["## Симулятор в браузере (WebAssembly)", "",
             f"- Роботы на ПК: {robots(pc_tests)}; в WebAssembly (Node.js): {robots(wasm_tests)}.",
             f"- Одинаковы с ПК: снимков {len(pictures)}, деревьев {len(trees)} (без \"mem\": "
             "WebAssembly — 32-битная сборка)."]
    if found["different"]:
        lines.append(f"- **Отличаются:** {', '.join(found['different'])}.")
    if found["missing"]:
        lines.append(f"- **Нет в WebAssembly:** {', '.join(found['missing'])}.")
    if page:
        sizes = ", ".join(f"`{name}` {mb(n) if n >= 1048576 else kb(n)}" for name, n in page)
        lines += [f"- Страница: {sizes}, всего {mb(sum(n for _, n in page))}."]
    return lines


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--head", required=True, help="TelaOS checkout to build and run")
    ap.add_argument("--out", required=True)
    ap.add_argument("--sim", help="simulator sources (default: <head>/sim)")
    ap.add_argument("--emsdk", help="emsdk folder with an active SDK (default: emcmake on PATH)")
    args = ap.parse_args()

    head = Path(args.head).resolve()
    out = Path(args.out).resolve()
    logs = out / "logs"
    shutil.rmtree(out / "runs", ignore_errors=True)
    shutil.rmtree(logs, ignore_errors=True)
    logs.mkdir(parents=True)
    sim_dir = Path(args.sim).resolve() if args.sim else head / "sim"
    fixtures = sim_dir / "tests" / "fixtures"
    env = emscripten_env(args.emsdk)
    if env is None:
        print("[wasm] no Emscripten: emcmake is not on PATH, and --emsdk has no active SDK", flush=True)
        return 1

    print("[wasm] building the simulator for the PC, for Node.js and the page ...", flush=True)
    pc = sim_screens.build(sim_dir, head, out / "build-pc", logs / "build-pc.log")
    js = build_wasm(sim_dir, head, out / "build-node", logs / "build-node.log", "node", env)
    html = build_wasm(sim_dir, head, out / "build-web", logs / "build-web.log", "web", env)
    for name, built in (("pc", pc), ("node", js), ("web", html)):
        if not built:
            sys.stdout.write((logs / f"build-{name}.log").read_text()[-4000:])
            print(f"[wasm] the {name} build failed", flush=True)
            return 1

    page_dir = out / "page"
    shutil.rmtree(page_dir, ignore_errors=True)
    page_dir.mkdir()
    page = []
    for ext in (".html", ".js", ".wasm", ".data"):
        f = html.with_suffix(ext)
        shutil.copy2(f, page_dir / f.name)
        page.append((f.name, f.stat().st_size))

    print("[wasm] robot tests on the PC and in WebAssembly ...", flush=True)
    pc_tests = sim_screens.run_tests(pc, sim_dir / "tests", fixtures, out / "runs" / "pc")
    wasm_tests = sim_screens.run_tests(node_runner(js, env), sim_dir / "tests", fixtures, out / "runs" / "wasm")
    found = compare(out / "runs" / "pc", out / "runs" / "wasm")

    lines = report(pc_tests, wasm_tests, found, page)
    (out / "wasm.md").write_text("\n".join(lines) + "\n")
    print("\n".join(lines), flush=True)
    failed = [t for t in pc_tests + wasm_tests if not t["ok"]]
    return 1 if failed or found["different"] or found["missing"] else 0


if __name__ == "__main__":
    sys.exit(main())
