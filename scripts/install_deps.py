import sys
import subprocess

try:
    from SCons.Script import Import
    Import("env")
    python_exe = env.subst("$PYTHONEXE")
except Exception:
    python_exe = sys.executable

print(f"[PIO-INFO] Checking Python dependencies using: {python_exe}")

try:
    from PIL import Image
except ImportError:
    print("[PIO-INFO] Pillow not found. Installing into the isolated environment...")
    try:
        subprocess.check_call([python_exe, "-m", "pip", "install", "Pillow"])
        print("[PIO-INFO] Pillow installed successfully!")
    except subprocess.CalledProcessError as e:
        print(f"[PIO-ERROR] Failed to install Pillow via pip: {e}")
        sys.exit(1)
