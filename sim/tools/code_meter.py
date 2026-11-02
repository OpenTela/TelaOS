#!/usr/bin/env python3
"""code_meter.py — how much C is left in the C++ sources.

    python3 sim/tools/code_meter.py <checkout> [<base checkout>]

Counts, in src/, the C-style spots that typed C++ replaces: string compares and searches
on raw pointers, printf into fixed buffers, string copies, fixed char arrays, raw
malloc/free. Comments and string literals do not count. With a base checkout: the change,
spot by spot, as the PR report shows it. Python standard library only.
"""

import re
import sys
from pathlib import Path

# (key, label in the report, pattern)
METRICS = [
    ("strcmp", "strcmp", re.compile(r"\bstrn?(?:case)?cmp\s*\(")),
    ("strfind", "strstr/strchr", re.compile(r"\bstr(?:str|chr|rchr|pbrk|spn|cspn)\s*\(")),
    ("printf", "snprintf", re.compile(r"\bv?sn?printf\s*\(")),
    ("strcpy", "strcpy/strcat", re.compile(r"\bstrn?(?:cpy|cat)\s*\(|\bstrdup\s*\(")),
    ("charbuf", "char x[N]", re.compile(r"\bchar\s+\w+\s*\[[^\]]+\]")),
    ("malloc", "malloc/free", re.compile(r"\b(?:malloc|calloc|realloc|free)\s*\(")),
]
SOURCES = ("*.cpp", "*.h", "*.hpp")  # src/*.c are generated font bitmaps
RAW = re.compile(r'R"([^()\\\s]{0,16})\(')


def code_only(text):
    """The source without comments, string and char literals (each literal becomes "")."""
    out, i, n = [], 0, len(text)
    while i < n:
        c = text[i]
        if text.startswith("//", i):
            j = text.find("\n", i)
            i = n if j < 0 else j
            continue
        if text.startswith("/*", i):
            j = text.find("*/", i + 2)
            i = n if j < 0 else j + 2
            out.append(" ")
            continue
        if c == "R" and text.startswith('R"', i) and (i == 0 or not (text[i - 1].isalnum() or text[i - 1] == "_")
                                                     or text[i - 1] in "uUL8"):
            m = RAW.match(text, i)
            if m:
                close = ")" + m.group(1) + '"'
                j = text.find(close, m.end())
                i = n if j < 0 else j + len(close)
                out.append('""')
                continue
        if c == "'" and 0 < i < n - 1 and text[i - 1].isalnum() and text[i + 1].isalnum():
            out.append(c)  # digit separator: 1'000'000
            i += 1
            continue
        if c in "\"'":
            j = i + 1
            while j < n and text[j] != c and text[j] != "\n":
                j += 2 if text[j] == "\\" else 1
            i = j + 1
            out.append('""')
            continue
        out.append(c)
        i += 1
    return "".join(out)


def count(text):
    code = code_only(text)
    return {key: len(rx.findall(code)) for key, _, rx in METRICS}


def measure(checkout):
    """{"files", "lines", "metrics": {key: n}, "top": [(n, path)]} for <checkout>/src."""
    root = Path(checkout) / "src"
    files = sorted({p for pat in SOURCES for p in root.rglob(pat) if p.is_file()})
    total = {key: 0 for key, _, _ in METRICS}
    per_file, lines = [], 0
    for p in files:
        text = p.read_text("utf-8", "replace")
        lines += text.count("\n")
        c = count(text)
        for key in total:
            total[key] += c[key]
        n = sum(c.values())
        if n:
            per_file.append((n, p.relative_to(root.parent).as_posix()))
    per_file.sort(key=lambda t: (-t[0], t[1]))
    return {"files": len(files), "lines": lines, "metrics": total, "top": per_file[:8]}


def signed(n):
    return f"+{n}" if n > 0 else f"−{-n}"


def summary(head, base=None):
    """One line for the PR report."""
    now = sum(head["metrics"].values())
    if base is None:
        return f"**Сишные места в src/:** {now}"
    was = sum(base["metrics"].values())
    parts = [f"`{label}` {signed(head['metrics'][key] - base['metrics'][key])}"
             for key, label, _ in METRICS if head["metrics"][key] != base["metrics"][key]]
    if not parts:
        return f"**Сишные места в src/:** {now}, столько же"
    return f"**Сишные места в src/:** {was} → {now} ({signed(now - was)}): " + ", ".join(parts)


def main(argv):
    if len(argv) not in (2, 3):
        print(__doc__.strip().splitlines()[2].strip())
        return 2
    head = measure(argv[1])
    base = measure(argv[2]) if len(argv) == 3 else None
    print(summary(head, base).replace("**", "").replace("`", ""))
    print(f"{head['files']} files, {head['lines']} lines")
    for key, label, _ in METRICS:
        print(f"  {label:15} {head['metrics'][key]}")
    for n, path in head["top"]:
        print(f"  {n:5}  {path}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
