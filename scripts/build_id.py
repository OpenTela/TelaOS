#!/usr/bin/env python3
"""
build_id.py - Pre-build script: the firmware's build id, the commit it is built from.

Writes $BUILD_DIR/generated/tela_build_id.h:

    #define TELA_BUILD_ID "fc686e0"

git rev-parse --short=7 HEAD, with "+" when tracked files differ from that commit. No git
(a source archive): "unknown". The header is written only when the id changes, and only
src/core/build_info.cpp includes it: a new commit recompiles one file.

sys info shows it as "fw", the crash log as "fw" of the boot that crashed.

Usage:
  PlatformIO: extra_scripts = pre:scripts/build_id.py (first: before other scripts touch files)
"""

import subprocess
from pathlib import Path


def build_id(project_dir):
    def git(*args):
        return subprocess.run(["git", "-C", str(project_dir), *args],
                              capture_output=True, text=True, timeout=10)
    try:
        head = git("rev-parse", "--short=7", "HEAD")
        if head.returncode != 0:
            return "unknown"
        changed = git("status", "--porcelain", "--untracked-files=no").stdout.strip()
        return head.stdout.strip() + ("+" if changed else "")
    except (OSError, subprocess.SubprocessError):
        return "unknown"


def write_header(out_dir, ident):
    header = Path(out_dir) / "tela_build_id.h"
    text = f'#pragma once\n// scripts/build_id.py\n#define TELA_BUILD_ID "{ident}"\n'
    header.parent.mkdir(parents=True, exist_ok=True)
    if not header.exists() or header.read_text() != text:
        header.write_text(text)
    return header


try:
    Import("env")  # noqa: F821 - PlatformIO's SCons
except NameError:
    env = None

if env is not None:
    ident = build_id(env.subst("$PROJECT_DIR"))
    out = Path(env.subst("$BUILD_DIR")) / "generated"
    write_header(out, ident)
    env.Append(CPPPATH=[str(out)])
    print(f"Build id: {ident}")
