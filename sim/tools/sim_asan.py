#!/usr/bin/env python3
"""sim_asan.py — the simulator under AddressSanitizer and UBSan: every robot, every screen,
the random pages. A memory error gives the right pixels as often as not (the slider map read
after it was freed, #67): this sees what the screens cannot.

    python3 sim/tools/sim_asan.py --head . --out build/asan [--pages 300] [--seed 1]

Builds sim/ of --head with -DTELA_SIM_ASAN=ON (sim/CMakeLists.txt), then runs on it the
robot tests (sim/tests/*.sim), every screen of every app as sim_screens.py shoots them, and
--pages random pages (gen_pages.py, the seed CI uses). Writes:

    out/asan.md      what the sanitizers said, by kind and place, with the first report in full
    out/logs/        the log of every run

Exit code 1 if it does not build, a robot test fails, a run stops, or a sanitizer reports
anything. Leaks are not looked for: what lives as long as the firmware (the DOM tree, the
registries of HTML boxes) is never freed on purpose. UBSan reports of vendored code that do
not count are in ubsan.supp, each with why. Python standard library only.
"""

import argparse
import os
import re
import shutil
import sys
from pathlib import Path

import gen_pages
import sim_pages
import sim_screens

# A sanitizer's report starts with one of these lines
REPORT = re.compile(r"^(==\d+==ERROR: (AddressSanitizer|LeakSanitizer)[^\n]*|[^\n]*: runtime error: [^\n]*)$", re.M)
FIRST_FRAME = re.compile(r"^\s+#\d+ 0x[0-9a-f]+ in (\S+) (\S+)$", re.M)  # function, file:line


def reports(log):
    """(kind, where) of every report in a log, and the first report in full: ASan's, if any"""
    found, first, first_asan = [], None, None
    for m in REPORT.finditer(log):
        line = m.group(1)
        if "runtime error:" in line:  # UBSan: file:line:col: runtime error: what
            where, what = line.split(": runtime error: ", 1)
            found.append(("UBSan: " + re.sub(r"\d+", "N", what)[:80], where.strip()))
        else:
            kind = re.sub(r"^==\d+==ERROR: ", "", line).split(" on ")[0].split(" at ")[0]
            frame = FIRST_FRAME.search(log, m.end())
            found.append((kind, f"{frame.group(1)} {frame.group(2)}" if frame else "?"))
        if first is None:
            first = log[m.start():m.start() + 6000]
        if first_asan is None and "runtime error:" not in line:
            first_asan = log[m.start():m.start() + 6000]
    return found, first_asan or first


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--head", required=True, help="TelaOS checkout to build and run")
    ap.add_argument("--out", required=True)
    ap.add_argument("--sim", help="simulator sources (default: <head>/sim)")
    ap.add_argument("--pages", type=int, default=300, help="random pages (gen_pages.py)")
    ap.add_argument("--seed", type=int, default=1)
    args = ap.parse_args()

    head = Path(args.head).resolve()
    out = Path(args.out).resolve()
    logs = out / "logs"
    shutil.rmtree(logs, ignore_errors=True)
    logs.mkdir(parents=True)
    sim_dir = Path(args.sim).resolve() if args.sim else head / "sim"
    fixtures = sim_dir / "tests" / "fixtures"
    # Reports to the log, all of them (UBSan goes on after one), no leaks (see above)
    os.environ["ASAN_OPTIONS"] = "detect_leaks=0:abort_on_error=0:halt_on_error=1"
    supp = Path(__file__).resolve().parent / "ubsan.supp"  # vendored code's harmless UB, each with why
    os.environ["UBSAN_OPTIONS"] = f"print_stacktrace=1:halt_on_error=0:suppressions={supp}"

    print("[asan] building the simulator with ASan and UBSan ...", flush=True)
    binary = sim_screens.build(sim_dir, head, out / "build", out / "build.log",
                               options=("-DTELA_SIM_ASAN=ON", "-DCMAKE_BUILD_TYPE=RelWithDebInfo"))
    if not binary:
        sys.stdout.write((out / "build.log").read_text()[-4000:])
        print("[asan] it does not build", flush=True)
        return 1

    print("[asan] robot tests ...", flush=True)
    tests = sim_screens.run_tests(binary, sim_dir / "tests", fixtures, logs)
    print("[asan] every screen ...", flush=True)
    stopped = sim_screens.shoot(binary, [head / "data", fixtures], fixtures, out / "shots", logs / "screens")
    if args.pages:
        print(f"[asan] {args.pages} random pages ...", flush=True)
        batches = gen_pages.write(out / "pages-src", args.pages, args.seed)
        stopped += sim_pages.draw(binary, batches, logs / "pages", os.cpu_count() or 2)

    found, first, runs = {}, None, 0
    for log in sorted(logs.rglob("*.log")):
        runs += 1
        text = log.read_text("utf-8", "replace").replace(f"{head}/", "")  # paths in the checkout
        rs, f = reports(text)
        for r in rs:
            found.setdefault(r, []).append(log.relative_to(logs).as_posix())
        if f and (first is None or ("runtime error:" in first and "runtime error:" not in f.splitlines()[0])):
            first = f
    failed = [t for t in tests if not t["ok"]]

    lines = ["## ASan и UBSan", "",
             f"Прогонов: {runs} — роботы ({len(tests)}), все экраны, случайные страницы: {args.pages}."]
    if not found and not failed and not stopped:
        lines.append("Ни одного сообщения санитайзеров, все роботы прошли.")
    if failed:
        lines.append("Роботы не прошли: " + ", ".join(f"`{t['name']}` ({t['passed']}/{t['passed'] + t['failed']})"
                                                       for t in failed))
    if stopped:
        lines.append("Прогон остановился: " + ", ".join(f"`{s}`" for s in stopped))
    if found:
        lines += ["", f"Сообщений: {sum(len(v) for v in found.values())}, разных: {len(found)}", ""]
        # ASan's first (a memory error), then UBSan's, the most frequent first
        for (kind, where), where_logs in sorted(found.items(), key=lambda kv: (kv[0][0].startswith("UBSan"), -len(kv[1]))):
            lines.append(f"- **{kind}** — `{where}` — {len(where_logs)} × (`{where_logs[0]}`)")
        lines += ["", "<details><summary>Первое сообщение целиком</summary>", "", "```", first.strip(), "```",
                  "", "</details>"]
    (out / "asan.md").write_text("\n".join(lines) + "\n")
    print("\n".join(lines), flush=True)
    return 1 if found or failed or stopped else 0


if __name__ == "__main__":
    sys.exit(main())
