#!/usr/bin/env python3
"""style_check.py — the rules of CONTRIBUTING «Код» that a robot can check, on the lines a PR adds.

    python3 .github/scripts/style_check.py [--base origin/future] [repo root]

- **The body of `if` on its own line.** `if (ok) return;`, `if (ok) { return; }`, `else return;`
  put the body on the line of the condition. After `if (…)` and after `else` the line holds `{`
  at most, and comments.
- **A long condition goes into a variable with a name.** Longer than LONG characters (spaces run
  together), or written on more than one line: long.
- **File names are unique** in the project's own code. The stubs of ci/stubs/ and sim/shim/ are
  named after the headers they replace, so they are left out.

The `if` rules look at the C and C++ of the project (not the third-party code of lib/, ci/lib/,
sim/third_party/) and only at the lines the PR adds or changes, `git diff BASE...HEAD`: old code
is fixed when it is touched. Comments, strings and preprocessor lines are not code here: a macro
is written on one line.

Prints «file:line: what», in GitHub Actions also as an annotation on that line of the PR, and
exits 1 if there is anything.
"""

import bisect
import os
import re
import subprocess
import sys
from pathlib import Path

LONG = 60
CODE = (".c", ".cc", ".cpp", ".h", ".hpp")
NAMED = CODE + (".py", ".sh")
THIRD_PARTY = ("lib/", "ci/lib/", "sim/third_party/")
SAME_NAME_ON_PURPOSE = ("ci/stubs/", "sim/shim/")

IF = re.compile(r"\bif\s*(?:constexpr\s*)?\(")
ELSE = re.compile(r"\belse\b")
RAW_OPEN = re.compile(r'([^ ()\\\t\n]{0,16})\(')
RAW_PREFIXES = ("R", "u8R", "uR", "UR", "LR")
CHAR_PREFIXES = ("L", "u", "U", "u8")

BODY = "тело if на строке условия: перенеси его на следующую строку"
ELSE_BODY = "тело else на строке else: перенеси его на следующую строку"
LONG_TEXT = "длинное условие ({n} знаков, можно до {long}): вынеси в переменную с именем"
LINES_TEXT = "условие на нескольких строках: вынеси в переменную с именем"


def blank(text):
    """The code of a C/C++ text: comments and preprocessor lines become spaces, the insides of
    strings and character literals `_`. Lines and columns stay where they were."""
    out = list(text)
    n = len(text)

    def wipe(a, b, fill):
        for k in range(a, min(b, n)):
            if out[k] != "\n":
                out[k] = fill

    def till(s, start):
        """The index of `s` from start, or the end of the text"""
        k = text.find(s, start)
        return n if k < 0 else k

    def token_before(i):
        k = i
        while k > 0 and (text[k - 1].isalnum() or text[k - 1] == "_"):
            k -= 1
        return text[k:i]

    def literal_end(i, quote):
        """The index of the quote that closes the literal opened at i (or of the line's end)"""
        j = i + 1
        while j < n and text[j] not in (quote, "\n"):
            j += 2 if text[j] == "\\" else 1
        return min(j, n)

    i, line_start = 0, True
    while i < n:
        c = text[i]
        if c == "\n":
            i, line_start = i + 1, True
            continue
        if line_start and c == "#":  # a directive, with its \-continued lines
            end = till("\n", i)
            while end < n and text[end - 1] == "\\":
                end = till("\n", end + 1)
            wipe(i, end, " ")
            i = end
            continue
        if not c.isspace():
            line_start = False
        if text.startswith("//", i):
            end = till("\n", i)
            wipe(i, end, " ")
            i = end
        elif text.startswith("/*", i):
            end = min(n, till("*/", i + 2) + 2)
            wipe(i, end, " ")
            i = end
        elif c == '"':
            raw = RAW_OPEN.match(text, i + 1) if token_before(i) in RAW_PREFIXES else None
            if raw:
                end = till(f"){raw.group(1)}\"", raw.end())
                wipe(raw.end(), end, "_")
                i = end + len(raw.group(1)) + 2
            else:
                end = literal_end(i, '"')
                wipe(i + 1, end, "_")
                i = end + 1
        elif c == "'":
            token = token_before(i)
            if token[:1].isdigit():  # 1'000'000: a digit separator
                i += 1
                continue
            end = literal_end(i, "'")
            wipe(i + 1, end, "_")
            i = end + 1
        else:
            i += 1
    return "".join(out)


def closing(code, i):
    """The index of the `)` that closes the `(` at i, -1 if there is none"""
    depth = 0
    for j in range(i, len(code)):
        if code[j] == "(":
            depth += 1
        elif code[j] == ")":
            depth -= 1
            if depth == 0:
                return j
    return -1


def problems(text, long=LONG):
    """[(first line, last line, line to report, what)] of a C/C++ text, lines from 1. First and
    last are the lines of the head of the statement: a PR that touches any of them owns it."""
    code = blank(text)
    starts = [0] + [m.end() for m in re.finditer("\n", code)]

    def line(pos):
        return bisect.bisect_right(starts, pos)

    def rest(pos):
        end = code.find("\n", pos)
        return code[pos:len(code) if end < 0 else end].strip()

    found = []
    for m in IF.finditer(code):
        end = closing(code, m.end() - 1)
        if end < 0:
            continue
        first, last = line(m.start()), line(end)
        if rest(end + 1) not in ("", "{"):
            found.append((first, last, last, BODY))
        condition = " ".join(code[m.end():end].split())
        if first != last:
            found.append((first, last, first, LINES_TEXT))
        elif len(condition) > long:
            found.append((first, last, first, LONG_TEXT.format(n=len(condition), long=long)))
    for m in ELSE.finditer(code):
        after = rest(m.end())
        if after not in ("", "{") and not IF.match(after):
            at = line(m.start())
            found.append((at, at, at, ELSE_BODY))
    return sorted(found, key=lambda p: (p[2], p[3]))


def added(root, base):
    """{path: {line numbers}}: the lines that `git diff BASE...HEAD` adds or changes"""
    diff = subprocess.run(["git", "-C", str(root), "diff", "-U0", "--no-color", "--no-ext-diff",
                           f"{base}...HEAD"], check=True, capture_output=True, text=True).stdout
    lines, path = {}, None
    for row in diff.splitlines():
        if row.startswith("+++ "):
            path = row[6:] if row.startswith("+++ b/") else None
            if path is not None:
                lines.setdefault(path, set())
        elif row.startswith("@@") and path is not None:
            m = re.match(r"@@ -\S+ \+(\d+)(?:,(\d+))? @@", row)
            start, count = int(m.group(1)), int(m.group(2) or 1)
            lines[path].update(range(start, start + count))
    return lines


def own_code(path):
    return path.endswith(CODE) and not path.startswith(THIRD_PARTY)


def same_names(files):
    """{name: [paths]}: the files of the project's own code that share a name"""
    by_name = {}
    for f in files:
        if f.endswith(NAMED) and not f.startswith(THIRD_PARTY + SAME_NAME_ON_PURPOSE):
            by_name.setdefault(f.rsplit("/", 1)[-1], []).append(f)
    return {name: paths for name, paths in sorted(by_name.items()) if len(paths) > 1}


def check(root, base):
    """[(path, line or None, what)] for the changes from base"""
    root = Path(root)
    found = []
    for path, lines in sorted(added(root, base).items()):
        if not own_code(path) or not lines:
            continue
        text = (root / path).read_text("utf-8", "replace")
        for first, last, at, what in problems(text):
            if lines.intersection(range(first, last + 1)):
                found.append((path, at, what))
    files = subprocess.run(["git", "-C", str(root), "ls-files"], check=True, capture_output=True,
                           text=True).stdout.splitlines()
    for name, paths in same_names(files).items():
        found += [(p, None, f"имя {name} уже есть: {', '.join(q for q in paths if q != p)}")
                  for p in paths]
    return found


def main(argv=None):
    argv = list(sys.argv[1:] if argv is None else argv)
    base = "origin/future"
    if "--base" in argv:
        k = argv.index("--base")
        base = argv[k + 1]
        del argv[k:k + 2]
    root = Path(argv[0]) if argv else Path(__file__).resolve().parents[2]
    found = check(root, base)
    actions = os.environ.get("GITHUB_ACTIONS") == "true"
    for path, at, what in found:
        print(f"{path}:{at}: {what}" if at else f"{path}: {what}")
        if actions:
            where = f"file={path},line={at}" if at else f"file={path}"
            print(f"::error {where}::{what}")
    print(f"style_check: {len(found)} problems" if found else "style_check: the new code keeps the rules")
    return 1 if found else 0


if __name__ == "__main__":
    sys.exit(main())
