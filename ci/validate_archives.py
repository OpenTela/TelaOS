#!/usr/bin/env python3
"""
validate_archives.py — Validate archive structure.

Two modes:
  --strict   Errors on build artifacts (for archive validation)
  (default)  Warnings on build artifacts (for dev-time checks)

Usage:
  python3 validate_archives.py <compiler_dir> [project_dir] [--strict]
"""

import sys
import os
from pathlib import Path
from collections import Counter

WARN = "⚠"
ERR = "✗"


def collect_files(root: Path) -> list:
    result = []
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames[:] = [d for d in dirnames if not d.startswith('.')]
        for f in filenames:
            if f.startswith('.'): continue
            full = Path(dirpath) / f
            rel = full.relative_to(root)
            result.append((str(rel), full.stat().st_size))
    return result


def find_stale_versions(paths, prefix_filter=None):
    issues = []
    versioned = {}
    for p in paths:
        if prefix_filter and not p.startswith(prefix_filter):
            continue
        name = Path(p).stem
        parts = name.rsplit('_v', 1)
        if len(parts) == 2:
            try:
                float(parts[1])
                versioned.setdefault(parts[0], []).append((parts[1], p))
            except ValueError:
                pass

    for base, vs in versioned.items():
        if len(vs) > 1:
            vs_sorted = sorted(vs, key=lambda x: float(x[0]))
            latest = vs_sorted[-1][1]
            stale = [v[1] for v in vs_sorted[:-1]]
            issues.append((ERR, f"STALE VERSIONS: {base} — keep: {latest}, remove: {', '.join(stale)}"))
    return issues


def validate_compiler(root: Path, strict: bool) -> list:
    issues = []
    files = collect_files(root)
    paths = [f[0] for f in files]
    sizes = {f[0]: f[1] for f in files}

    # --- Build artifacts (warning in dev, error in strict/archive) ---
    level = ERR if strict else WARN
    for fd in ['build', 'tests/bin']:
        matching = [p for p in paths if p.startswith(fd + '/')]
        if matching:
            total_kb = sum(sizes[p] for p in matching) // 1024
            issues.append((level, f"{fd}/ — {len(matching)} files, {total_kb} KB (excluded from zip)"))

    # --- Required structure ---
    top_dirs = {Path(p).parts[0] for p in paths if len(Path(p).parts) > 1}
    for d in ['stubs', 'tests', 'lib']:
        if d not in top_dirs:
            issues.append((ERR, f"MISSING DIR: {d}/"))

    root_files = {Path(p).name for p in paths if '/' not in p}
    for f in ['test.py', 'build.py']:
        if f not in root_files:
            issues.append((ERR, f"MISSING FILE: {f}"))

    # --- .o/.d outside build/ ---
    stray = [p for p in paths if (p.endswith('.o') or p.endswith('.d'))
             and not p.startswith('build/') and not p.startswith('tests/bin/')]
    if stray:
        issues.append((ERR, f"STRAY OBJECTS: {len(stray)} .o/.d outside build/"))

    # --- Large files (ignoring known dirs) ---
    for p, sz in sizes.items():
        if p.startswith('build/') or p.startswith('tests/bin/'): continue
        if Path(p).suffix in {'.a', '.so'}: continue
        if sz > 200 * 1024 and Path(p).suffix in {'.cpp', '.h', '.py', '.md'}:
            issues.append((WARN, f"LARGE: {p} ({sz//1024}KB)"))
        if sz > 50 * 1024 and Path(p).suffix == '':
            issues.append((ERR, f"BINARY: {p} ({sz//1024}KB)"))

    issues.extend(find_stale_versions(paths))
    return issues


def validate_esp(root: Path, strict: bool) -> list:
    issues = []
    files = collect_files(root)
    paths = [f[0] for f in files]

    # --- Required ---
    top_dirs = {Path(p).parts[0] for p in paths if len(Path(p).parts) > 1}
    if 'src' not in top_dirs:
        issues.append((ERR, "MISSING DIR: src/"))

    # --- Build artifacts ---
    artifacts = [p for p in paths if p.endswith('.o') or p.endswith('.d') or 'build/' in p]
    if artifacts:
        issues.append((ERR, f"BUILD ARTIFACTS: {len(artifacts)} files"))

    # --- Unexpected top dirs ---
    allowed_top = {'src', 'docs', 'include', 'data'}
    unexpected = set()
    for p in paths:
        parts = Path(p).parts
        if len(parts) > 1 and parts[0] not in allowed_top:
            unexpected.add(parts[0])
    if unexpected:
        issues.append((ERR, f"UNEXPECTED DIRS: {', '.join(sorted(unexpected))}/"))

    # --- Duplicate filenames (project rule) ---
    basenames = [Path(p).name for p in paths if Path(p).suffix in {'.cpp', '.h'}]
    dupes = {n: c for n, c in Counter(basenames).items() if c > 1}
    for name, count in dupes.items():
        locs = [p for p in paths if Path(p).name == name]
        dirs = set(str(Path(p).parent) for p in locs)
        if len(dirs) > 1:
            issues.append((ERR, f"DUPLICATE: {name} ({count}x) in {', '.join(sorted(dirs))}"))

    # --- Stale versioned docs ---
    issues.extend(find_stale_versions(paths, prefix_filter='docs/'))

    return issues


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('-')]
    strict = '--strict' in sys.argv

    if not args:
        print("Usage: validate_archives.py <compiler_dir> [esp_dir] [--strict]")
        return 1

    has_errors = False
    for path_str in args:
        root = Path(path_str)
        if not root.exists():
            print(f"  {ERR} {root} not found")
            has_errors = True
            continue

        name = root.name
        if name == 'compiler':
            issues = validate_compiler(root, strict)
        elif name == 'esp':
            issues = validate_esp(root, strict)
        else:
            continue

        errors = [i for i in issues if i[0] == ERR]
        warns = [i for i in issues if i[0] == WARN]

        if errors:
            print(f"  {ERR} {name}/ — {len(errors)} error(s)" +
                  (f", {len(warns)} warning(s)" if warns else ""))
            for level, msg in issues:
                print(f"    {level} {msg}")
            has_errors = True
        elif warns:
            print(f"  ✓ {name}/ — ok ({len(warns)} warning(s))")
            for level, msg in warns:
                print(f"    {level} {msg}")
        else:
            print(f"  ✓ {name}/ — clean")

    return 1 if has_errors else 0


if __name__ == '__main__':
    sys.exit(main())
