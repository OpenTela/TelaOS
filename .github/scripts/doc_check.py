#!/usr/bin/env python3
"""doc_check.py — do the documents say what the code and their own tables say?

    python3 .github/scripts/doc_check.py [repo root]

Checks, each one dumb and exact:
- **versions:** a document with `vX.Y` in its title has a `## Changelog` whose first entry is
  that version, and the entries go down without repeats. Two PRs that bump one document
  meet at a merge; a wrong resolution leaves the title and the Changelog apart;
- **coverage:** «## Работает: N из M» (docs/CSS.md, docs/DOM.md) counts its own tables: N rows
  ✅, M rows with a mark at all (✅, ½, —, ❌);
- **protocol:** the version of docs/CONSOLE_PROTOCOL_SPEC.md is the protocol's, the one the
  watch gives in `sys sync`: `PROTOCOL_VERSION` of src/console/console.cpp and of
  tools/ble_assistant.py say the same;
- **log:** the categories and levels in docs/CONSOLE_PROTOCOL_SPEC.md are the ones of
  src/utils/log_config.h.

Prints one line per problem, «file: what», and exits 1 if there is any. The documents are the
repository's own `*.md` (`git ls-files`), without third-party code (lib/, ci/lib/).
"""

import re
import subprocess
import sys
from pathlib import Path

VERSION = r"v(\d+(?:\.\d+)+)"
MARKS = ("✅", "½", "—", "❌")


def key(version):
    return tuple(int(x) for x in version.split("."))


def section(text, heading_re):
    """The text under the first heading that matches, up to the next `## ` heading or `---`"""
    m = re.search(heading_re, text, re.M)
    if not m:
        return None
    rest = text[m.end():]
    end = re.search(r"^(## |---\s*$)", rest, re.M)
    return rest[:end.start()] if end else rest


def versions(text):
    """The problems with a document's version, [] for an unversioned one"""
    title = text.split("\n", 1)[0]
    m = re.search(rf"\b{VERSION}\b", title) if title.startswith("# ") else None
    if not m:
        return []
    log = section(text, r"^## Changelog\s*$")
    entries = re.findall(rf"^- \*\*{VERSION}\*\*", log or "", re.M)
    if not entries:
        return [f"в заголовке v{m.group(1)}, а Changelog с версиями нет"]
    problems = []
    if entries[0] != m.group(1):
        problems.append(f"в заголовке v{m.group(1)}, а первая запись Changelog — v{entries[0]}")
    for a, b in zip(entries, entries[1:]):
        if key(a) <= key(b):
            problems.append(f"Changelog не по убыванию: v{a}, а за ней v{b}")
    return problems


def coverage(text):
    """«## Работает: N из M» against its tables"""
    problems = []
    for m in re.finditer(r"^## Работает: (\d+) из (\d+)\s*$", text, re.M):
        body = section(text[m.start():], r"^## Работает:.*$")
        marks = re.findall(rf"^\|\s*({'|'.join(MARKS)})\s*\|", body, re.M)
        works, total = marks.count("✅"), len(marks)
        if (works, total) != (int(m.group(1)), int(m.group(2))):
            problems.append(f"«Работает: {m.group(1)} из {m.group(2)}», а в таблицах ✅ {works} из {total}")
    return problems


def enum_names(header, name):
    """The members of `enum <name>` in C++ source, lower case, without COUNT"""
    m = re.search(rf"enum\s+{name}\b[^{{]*\{{([^}}]*)\}}", header)
    if not m:
        return None
    names = [re.sub(r"\s*=.*", "", x).strip() for x in m.group(1).split(",")]
    return [n.lower() for n in names if n and n != "COUNT"]


def listed(spec, label):
    """The `words` after «**<label>:**» in a document"""
    m = re.search(rf"\*\*{label}:\*\*(.*)", spec)
    return re.findall(r"`([^`]+)`", m.group(1)) if m else None


def log_names(header, spec):
    """The log categories and levels of the protocol against log_config.h"""
    problems = []
    for enum, label in (("Cat", "Категории"), ("Level", "Уровни")):
        code, doc = enum_names(header, enum), listed(spec, label)
        if code is None or doc is None:
            problems.append(f"не нашлось: enum {enum} в коде или «{label}» в протоколе")
            continue
        missing, extra = [n for n in code if n not in doc], [n for n in doc if n not in code]
        if missing:
            problems.append(f"«{label}»: в коде есть, в протоколе нет — {', '.join(missing)}")
        if extra:
            problems.append(f"«{label}»: в протоколе есть, в коде нет — {', '.join(extra)}")
    return problems


def protocol(spec, sources):
    """The spec's version against PROTOCOL_VERSION in each source: {path: text}"""
    m = re.search(rf"\b{VERSION}\b", spec.split("\n", 1)[0])
    if not m:
        return ["в заголовке нет версии протокола"]
    problems = []
    for path, text in sources.items():
        code = re.search(r"PROTOCOL_VERSION\s*=\s*\"([^\"]+)\"", text)
        if not code:
            problems.append(f"в {path} нет PROTOCOL_VERSION")
        elif code.group(1) != m.group(1):
            problems.append(f"протокол v{m.group(1)}, а в {path} — {code.group(1)}")
    return problems


def documents(root):
    out = subprocess.run(["git", "-C", str(root), "ls-files", "*.md"], check=True, capture_output=True,
                         text=True).stdout.split()
    return [f for f in out if not f.startswith(("lib/", "ci/lib/"))]


def check(root):
    """[(file, problem)] for the whole repository"""
    root = Path(root)
    problems = []
    for name in documents(root):
        text = (root / name).read_text("utf-8", "replace")
        problems += [(name, p) for p in versions(text) + coverage(text)]
    spec = "docs/CONSOLE_PROTOCOL_SPEC.md"
    header = (root / "src/utils/log_config.h").read_text("utf-8", "replace")
    spec_text = (root / spec).read_text("utf-8", "replace")
    problems += [(spec, p) for p in log_names(header, spec_text)]
    sources = {path: (root / path).read_text("utf-8", "replace")
               for path in ("src/console/console.cpp", "tools/ble_assistant.py")}
    problems += [(spec, p) for p in protocol(spec_text, sources)]
    return problems


def main(argv=None):
    argv = sys.argv[1:] if argv is None else argv
    root = Path(argv[0]) if argv else Path(__file__).resolve().parents[2]
    problems = check(root)
    for name, problem in problems:
        print(f"{name}: {problem}")
    print(f"doc_check: {len(problems)} problems" if problems else "doc_check: the documents agree")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
