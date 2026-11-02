#!/usr/bin/env python3
"""sim_pages.py — random pages through two builds of the firmware: did anything change?

    python3 sim/tools/sim_pages.py --base-sim <telaos_sim> --head-sim <telaos_sim> --out <dir> \
        [--count 300] [--seed 1]

The pages come from gen_pages.py. The base draws each page once, the head twice: a page the
head draws differently on its two runs is unstable (it reads memory nobody wrote, say) and is
reported as such. The other pages are compared with the base by widget tree (what changed,
in words: tree_diff.py) and by pixels.

sim_screens.py runs this in every PR (--pages). Python standard library only.
"""

import argparse
import concurrent.futures
import hashlib
import json
import os
import shutil
import sys
from pathlib import Path

import gen_pages
from tree_diff import describe

PAGE_MS = 600   # virtual ms after "app launch": the page is drawn
MAX_LISTED = 8  # changed pages described in the report


def script(apps):
    lines = ["wait 500"]
    for app in apps:
        lines += [f"> app launch {app}", f"wait {PAGE_MS}", f"tree {app}", f"shot {app}"]
    return "\n".join(lines) + "\n"


def draw(binary, batches, shots, jobs):
    """Every page of every batch drawn by `binary` into `shots`; the batches that failed"""
    import sim_screens  # the one way to run the simulator (it imports this module too)
    shots.mkdir(parents=True, exist_ok=True)

    def one(batch):
        apps = sorted(p.name for p in (batch / "apps").iterdir())
        sim = shots / f"{batch.name}.sim"
        sim.write_text(script(apps))
        rc, log = sim_screens.run_sim(binary, shots, script=sim, extra_data=batch, timeout=600)
        (shots / f"{batch.name}.log").write_text(log)
        return batch.name, rc

    with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as ex:
        return [name for name, rc in ex.map(one, batches) if rc != 0]


def drawn(shots, app):
    """(widget tree without memory and time, digest of the picture); None if not drawn"""
    try:
        tree = json.loads((shots / f"{app}.json").read_text("utf-8"))
        png = hashlib.sha256((shots / f"{app}.png").read_bytes()).hexdigest()
    except (OSError, ValueError):
        return None
    tree.pop("mem", None)      # memory: the screens report has it
    tree.pop("time_ms", None)  # when, not what
    return tree, png


def changed_box(a, b):
    """Bounding box (x1, y1, x2, y2) of the pixels that differ between two pictures"""
    import sim_screens
    w, h, pa = sim_screens.read_png(a)
    _, _, pb = sim_screens.read_png(b)
    box = None
    for y in range(h):
        ra, rb = pa[y * w * 3:(y + 1) * w * 3], pb[y * w * 3:(y + 1) * w * 3]
        if ra != rb:
            xs = [x for x in range(w) if ra[x * 3:x * 3 + 3] != rb[x * 3:x * 3 + 3]]
            box = (xs[0], y, xs[-1], y) if box is None else (min(box[0], xs[0]), box[1], max(box[2], xs[-1]), y)
    return box


def widget_at(tree, box):
    """The innermost widget holding the whole box (a clipped child counts: it is what is drawn),
    as `type #id` or `type в type #id` of its nearest named parent"""
    best = None

    def walk(n, depth, named):
        nonlocal best
        x, y, w, h = n.get("x", 0), n.get("y", 0), n.get("w", 0), n.get("h", 0)
        here = n.get("type", "?") + (f" #{n['name']}" if n.get("name") else "")
        if x <= box[0] and y <= box[1] and box[2] < x + w and box[3] < y + h:
            key = (depth, -w * h)
            if best is None or key > best[0]:
                best = (key, here if n.get("name") or not named else f"{here} в {named}")
        for c in n.get("children", []):
            walk(c, depth + 1, here if n.get("name") else named)
    if tree and box:
        walk(tree.get("screen", tree), 0, None)
    return best[1] if best else None


def check(base_bin, head_bin, out, count=300, seed=1, jobs=None):
    """Draws `count` pages: once by the base, twice by the head. Returns what differs."""
    out = Path(out)
    shutil.rmtree(out, ignore_errors=True)
    jobs = jobs or os.cpu_count() or 2
    batches = gen_pages.write(out / "src", count, seed)
    runs = {"base": base_bin, "head-1": head_bin, "head-2": head_bin}
    failed = [f"{run}/{b}" for run, binary in runs.items() for b in draw(binary, batches, out / run, jobs)]

    result = {"count": count, "seed": seed, "same": 0, "changed": [], "unstable": [], "missing": [], "failed": failed}
    for i in range(count):
        app = gen_pages.page_name(i)
        base, h1, h2 = (drawn(out / run, app) for run in runs)
        if not (base and h1 and h2):
            result["missing"].append(app)
        elif h1 != h2:
            box = changed_box(out / "head-1" / f"{app}.png", out / "head-2" / f"{app}.png") if h1[1] != h2[1] else None
            result["unstable"].append({"page": app, "where": widget_at(h1[0], box)})
        elif base == h1:
            result["same"] += 1
        else:
            words = describe(base[0], h1[0]) if base[0] != h1[0] else []
            if not words:
                where = widget_at(h1[0], changed_box(out / "base" / f"{app}.png", out / "head-1" / f"{app}.png"))
                words = ["Виджеты те же — изменились только пиксели" + (f", в `{where}`" if where else "")]
            result["changed"].append({"page": app, "changes": words})
    return result


def lines(result):
    """The report section: one line when nothing changed"""
    if not result:
        return []
    n, changed = result["count"], result["changed"]
    regen = f"`gen_pages.py --seed {result['seed']}`"
    if not changed:
        out = [f"**Случайные страницы:** {result['same']} из {n} без изменений ({regen})"]
    else:
        out = [f"**Случайные страницы:** изменилось {len(changed)} из {n} ({regen})", ""]
        for c in changed[:MAX_LISTED]:
            words = c["changes"]
            if len(words) == 1:
                w = words[0] if words[0][1:2].isupper() else words[0][:1].lower() + words[0][1:]
                out.append(f"- **{c['page']}**: {w}")
            else:
                out.append(f"- **{c['page']}**")
                out += [f"    - {w}" for w in words]
        if len(changed) > MAX_LISTED:
            out.append(f"- И ещё {len(changed) - MAX_LISTED}")
    out.append("")
    if result["unstable"]:
        pages = ", ".join(f"**{u['page']}**" + (f" (в `{u['where']}`)" if u["where"] else "")
                          for u in result["unstable"][:MAX_LISTED])
        out += [f"⚠️ Нестабильные: {len(result['unstable'])} — эта сборка рисует их по-разному от запуска "
                f"к запуску: {pages}", ""]
    if result["missing"]:
        out += [f"❌ Не нарисовались: {len(result['missing'])} — симулятор остановился (лог в артефакте): "
                + ", ".join(result["missing"][:MAX_LISTED]), ""]
    return out


def summary(result):
    """For status.json"""
    if not result:
        return None
    return {"count": result["count"], "seed": result["seed"], "same": result["same"],
            "changed": [c["page"] for c in result["changed"]], "unstable": [u["page"] for u in result["unstable"]],
            "missing": result["missing"], "failed": result["failed"]}


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--base-sim", required=True)
    ap.add_argument("--head-sim", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--count", type=int, default=300)
    ap.add_argument("--seed", type=int, default=1)
    args = ap.parse_args()
    result = check(Path(args.base_sim).resolve(), Path(args.head_sim).resolve(), Path(args.out).resolve(),
                   args.count, args.seed)
    print("\n".join(lines(result)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
