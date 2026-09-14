#!/usr/bin/env python3
"""
TelaOS Test Runner

Usage: python3 test.py <project_path> [options] [test_names...]

Options:
  -q, --quick     Run only key tests (~10 from each category, 5x faster)
                  On failure, auto-expands to full suite unless --no-expand
  --no-expand     In quick mode, don't auto-expand on failure
  -e, --exact     Exact test name match (default: partial match)
  -l, --list      List available tests (add -q to see quick set)
  -c, --clean     Clean test binaries before running
  --validate      Check archive structure (no zip)
  --pack          Validate + create compiler.zip in /outputs
  -h, --help      Show this help

Examples:
  python3 test.py /path/to/project              # run all tests
  python3 test.py /path/to/project -q           # quick smoke test
  python3 test.py /path/to/project css           # tests matching 'css'
  python3 test.py /path/to/project --pack        # validate & zip
"""

import subprocess
import sys
import shutil
import time
from pathlib import Path

COMPILER_DIR = Path(__file__).parent
TESTS_DIR = COMPILER_DIR / "tests"
BIN_DIR = TESTS_DIR / "bin"
MOCK_DIR = COMPILER_DIR / "build" / "mock"

LUA_LIB = COMPILER_DIR / "lib" / "lua54" / "build" / "liblua54.a"
LUA_INCLUDE = COMPILER_DIR / "lib" / "lua54" / "include"
KDL_LIB = COMPILER_DIR / "lib" / "ckdl" / "build" / "libkdl.a"

CXX = "g++"
STD = "gnu++2a"

# Quick mode: key tests per category for fast smoke check
# Picked for max coverage with min tests (~9 tests vs ~40 full)
QUICK_TESTS = {
    # core
    "state_store",
    # e2e (these two alone exercise console+UI+Lua+state+binding+onclick)
    "calc_console",
    "crossword",
    # lua
    "lua_errors",
    # ui
    "css",
    # widgets
    "binding",
    # e2e
    "lifecycle",
    # console
    "console_args",
    # parser
    "html_parser",
    # ble / ota (firmware update path)
    "bin_stream",
    "ota_receive",
}


class TestInfo:
    def __init__(self, src_path: Path):
        self.src = src_path
        self.stem = src_path.stem[5:]  # remove "test_" prefix
        rel = src_path.parent.relative_to(TESTS_DIR)
        self.folder = str(rel) if str(rel) != "." else ""
        self.display = f"{self.folder}/{self.stem}" if self.folder else self.stem

    def bin_path(self) -> Path:
        if self.folder:
            return BIN_DIR / self.folder / f"test_{self.stem}"
        return BIN_DIR / f"test_{self.stem}"


def find_tests() -> list:
    tests = []
    for f in sorted(TESTS_DIR.rglob("test_*.cpp")):
        tests.append(TestInfo(f))
    return sorted(tests, key=lambda t: t.display)


def get_includes(project_dir: Path) -> list:
    return [
        str(LUA_INCLUDE),
        str(project_dir / "src"),
        str(COMPILER_DIR / "stubs"),
        str(COMPILER_DIR / "lib" / "ckdl" / "include"),
        str(COMPILER_DIR / "lib" / "ckdl" / "bindings" / "cpp" / "include"),
    ]


def compile_test(test: TestInfo, project_dir: Path, objs: list) -> tuple:
    externals = TESTS_DIR / "externals.cpp"
    out = test.bin_path()
    out.parent.mkdir(parents=True, exist_ok=True)

    cmd = [CXX, f"-std={STD}", "-DLVGL_MOCK_ENABLED"]
    cmd += [f"-I{inc}" for inc in get_includes(project_dir)]
    cmd += [str(test.src), str(externals)]
    cmd += [str(o) for o in objs]
    cmd += [str(KDL_LIB), str(LUA_LIB)]
    cmd += ["-lmbedcrypto"]  # crypto_engine.o (SHA/HMAC/PBKDF2/AES-GCM)
    cmd += ["-lm", "-ldl"]
    cmd += ["-o", str(out)]

    r = subprocess.run(cmd, capture_output=True, text=True)
    return (True, "") if r.returncode == 0 else (False, r.stderr[:500])


def run_test(test: TestInfo) -> tuple:
    binary = test.bin_path()
    if not binary.exists():
        return False, "Binary not found"

    r = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30)
    output = r.stdout + r.stderr
    
    # Exit code is the primary signal
    if r.returncode != 0:
        return False, output
    
    # Fallback heuristics for tests that always return 0
    failed = ("FAILED" in output or "FAIL" in output)
    return (not failed), output


def validate_archives(project_dir: Path) -> int:
    """Run archive validation. Returns number of errors."""
    validate_script = COMPILER_DIR / "validate_archives.py"
    if not validate_script.exists():
        print("  ⚠ validate_archives.py not found, skipping")
        return 0

    r = subprocess.run(
        [sys.executable, str(validate_script), str(COMPILER_DIR), str(project_dir)],
        capture_output=True, text=True
    )
    print(r.stdout, end="")
    return r.returncode


def pack_archives(project_dir: Path) -> int:
    """Create compiler.zip for download. Project lives in GitHub — never packaged here."""
    import tempfile

    out_dir = Path("/mnt/user-data/outputs")
    out_dir.mkdir(parents=True, exist_ok=True)

    # compiler.zip — exclude build/, tests/bin/
    # Always zip as 'compiler/' regardless of actual dir name
    compiler_zip = out_dir / "compiler.zip"
    compiler_zip.unlink(missing_ok=True)
    with tempfile.TemporaryDirectory() as link_dir:
        link = Path(link_dir) / "compiler"
        link.symlink_to(COMPILER_DIR)
        r = subprocess.run(
            ["zip", "-qr", str(compiler_zip), "compiler/",
             "-x", "compiler/build/*", "compiler/tests/bin/*"],
            cwd=link_dir, capture_output=True, text=True
        )
    if r.returncode != 0:
        print(f"✗ zip compiler failed: {r.stderr}")
        return 1

    # Validate zip contents
    print("=== Archive Validation ===\n")
    with tempfile.TemporaryDirectory() as tmp:
        tmp = Path(tmp)
        subprocess.run(["unzip", "-qo", str(compiler_zip), "-d", str(tmp)],
                       capture_output=True)

        validate_script = COMPILER_DIR / "validate_archives.py"
        r = subprocess.run(
            [sys.executable, str(validate_script), "--strict",
             str(tmp / "compiler"), str(project_dir)],
            capture_output=True, text=True
        )
        print(r.stdout, end="")
        if r.returncode != 0:
            print(f"\n✗ Archive validation failed")
            return 1

    c_size = compiler_zip.stat().st_size / 1024
    print(f"\n✓ Packed:")
    print(f"  compiler.zip  {c_size:.0f} KB")
    return 0


def run_suite(selected: list, project_dir: Path, objs: list, label: str = "") -> list:
    """Run a list of tests. Returns list of (test, passed) tuples."""
    n = len(selected)
    if label:
        print(f"\n{'=' * 50}")
        print(f"  {label}")
        print(f"{'=' * 50}\n")
    print(f"Running {n} test(s), {len(objs)} objects\n")

    results = []
    for i, test in enumerate(selected, 1):
        print(f"[{i}/{n}] {test.display}: ", end="", flush=True)

        ok, err = compile_test(test, project_dir, objs)
        if not ok:
            print(f"COMPILE ERROR\n  {err[:200]}")
            results.append((test, False))
            continue

        try:
            passed, output = run_test(test)
        except subprocess.TimeoutExpired:
            print("TIMEOUT")
            results.append((test, False))
            continue

        if passed:
            for line in output.split("\n"):
                if "PASSED" in line or "Complete" in line:
                    print(line.strip()); break
            else:
                print("✓")
        else:
            print("✗ FAILED")
            for line in output.split("\n")[-10:]:
                if line.strip():
                    print(f"    {line}")

        results.append((test, passed))

    return results


def print_summary(results: list, elapsed: float):
    passed_count = sum(1 for _, p in results if p)
    n = len(results)
    failed_count = n - passed_count

    print()
    if failed_count == 0:
        print(f"✓ ALL {passed_count} TESTS PASSED  ({elapsed:.1f}s)")
    else:
        print(f"✗ {failed_count}/{n} FAILED  ({elapsed:.1f}s):")
        for t, p in results:
            if not p: print(f"  - {t.display}")
    return failed_count


def main():
    args = sys.argv[1:]

    if not args or "-h" in args or "--help" in args:
        print(__doc__)
        return 0 if "-h" in args or "--help" in args else 1

    project_dir = Path(args[0])
    args = args[1:]

    # --pack: validate + create archives
    if "--pack" in args:
        return pack_archives(project_dir)

    # --validate: just check structure
    if "--validate" in args:
        print("=== Archive Validation ===\n")
        return validate_archives(project_dir)

    for path, msg, hint in [
        (project_dir / "src", f"No src/ in {project_dir}", None),
        (MOCK_DIR, "build/mock/ not found", f"Run: python3 build.py {project_dir} --mock"),
        (LUA_LIB, "liblua54.a not found", "Run: ./lib/build_libs.sh"),
        (KDL_LIB, "libkdl.a not found", "Run: ./lib/build_libs.sh"),
    ]:
        if not path.exists():
            print(f"ERROR: {msg}")
            if hint: print(hint)
            return 1

    quick = "-q" in args or "--quick" in args
    no_expand = "--no-expand" in args
    exact = "-e" in args or "--exact" in args
    clean = "-c" in args or "--clean" in args
    list_only = "-l" in args or "--list" in args
    args = [a for a in args if not a.startswith("-")]

    all_tests = find_tests()

    if list_only:
        if quick:
            quick_set = [t for t in all_tests if t.stem in QUICK_TESTS]
            print(f"Quick tests ({len(quick_set)}/{len(all_tests)}):")
            for t in quick_set:
                print(f"  {t.display}")
            print(f"\nSkipped ({len(all_tests) - len(quick_set)}):")
            for t in all_tests:
                if t.stem not in QUICK_TESTS:
                    print(f"  {t.display}")
        else:
            current_folder = None
            print(f"Available tests ({len(all_tests)}):")
            for t in all_tests:
                folder = t.folder or "(root)"
                if folder != current_folder:
                    current_folder = folder
                    print(f"\n  {folder}/")
                q = " ★" if t.stem in QUICK_TESTS else ""
                print(f"    {t.stem}{q}")
        return 0

    if clean:
        if BIN_DIR.exists():
            shutil.rmtree(BIN_DIR)
        print("Cleaned")

    # Select tests
    if args:
        if exact:
            selected = [t for t in all_tests if t.stem in args]
            known_stems = {t.stem for t in all_tests}
            unknown = [a for a in args if a not in known_stems]
            if unknown:
                print(f"Unknown: {unknown}"); return 1
        else:
            selected = []
            for arg in args:
                matches = [t for t in all_tests
                           if arg in t.display or arg in t.stem]
                if matches:
                    selected.extend(matches)
                else:
                    print(f"No tests matching '{arg}'"); return 1
            seen = set()
            deduped = []
            for t in selected:
                if t.display not in seen:
                    seen.add(t.display)
                    deduped.append(t)
            selected = deduped
    elif quick:
        selected = [t for t in all_tests if t.stem in QUICK_TESTS]
        print(f"⚡ Quick mode: {len(selected)}/{len(all_tests)} tests")
    else:
        selected = all_tests

    objs = sorted(MOCK_DIR.rglob("*.o"))
    t0 = time.time()

    results = run_suite(selected, project_dir, objs)
    elapsed = time.time() - t0
    failed_count = print_summary(results, elapsed)

    # Quick mode auto-expand: on failure, re-run ALL tests
    if quick and failed_count > 0 and not no_expand:
        remaining = [t for t in all_tests if t.stem not in QUICK_TESTS]
        if remaining:
            print(f"\n⚡ Quick failed → expanding to full suite (+{len(remaining)} tests)")
            t1 = time.time()
            extra = run_suite(remaining, project_dir, objs, "Full expansion")
            all_results = results + extra
            total_elapsed = time.time() - t0
            failed_count = print_summary(all_results, total_elapsed)

    if failed_count == 0 and not args:
        print(f"\n=== Archive Validation ===\n")
        validate_archives(project_dir)

    return 1 if failed_count else 0


if __name__ == "__main__":
    sys.exit(main())
