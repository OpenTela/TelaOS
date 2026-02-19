#!/usr/bin/env python3
"""
resize_icons.py - Prepare app icons for embedding

Scans data/apps/*/icon.png and ensures all icons are:
- Correct size (64x64 by default)
- Trimmed to content (transparent padding removed)
- Properly formatted PNG with alpha

Usage:
  python scripts/resize_icons.py              # Process all icons
  python scripts/resize_icons.py -d icon.png  # Debug: creates ~icon_resized.png
  python scripts/resize_icons.py --size 48    # Custom size
"""

import sys
import shutil
import argparse
from pathlib import Path

try:
    from PIL import Image
except ImportError:
    print("ERROR: Pillow required. Install: pip install Pillow")
    sys.exit(1)

DEFAULT_SIZE = 64


def cleanup_debug_files(apps_dir):
    """Remove all ~*.png debug files"""
    count = 0
    for png in apps_dir.rglob("~*.png"):
        png.unlink()
        count += 1
    return count


def get_content_bbox(img, alpha_threshold=32):
    """Find bounding box of non-transparent content
    
    alpha_threshold: pixels with alpha <= this are considered transparent
                     (helps ignore faint shadows/blur)
    """
    if img.mode != 'RGBA':
        img = img.convert('RGBA')
    
    # Get alpha channel and apply threshold
    alpha = img.split()[3]
    # Convert to binary: 255 if alpha > threshold, else 0
    alpha = alpha.point(lambda x: 255 if x > alpha_threshold else 0)
    return alpha.getbbox()


def process_icon(png_path, target_size=DEFAULT_SIZE, debug=False, backup_dir=None, backup_name=None):
    """Process single icon: trim, resize, ensure alpha"""
    img = Image.open(png_path)
    original_size = img.size
    original_mode = img.mode
    
    # Quick check: if already exact size, skip entirely (no backup, no rewrite)
    if original_size == (target_size, target_size):
        return False, "ok"
    
    # Convert to RGBA
    if img.mode != 'RGBA':
        img = img.convert('RGBA')
    
    # Find content bounding box
    bbox = get_content_bbox(img)
    
    if bbox is None:
        return False, "fully transparent"
    
    content_w = bbox[2] - bbox[0]
    content_h = bbox[3] - bbox[1]
    
    # Check if content fills the whole image at target size
    content_fills_image = (bbox == (0, 0, img.width, img.height))
    already_correct_size = (img.size == (target_size, target_size))
    
    # Already perfect? Only if content fills AND size is correct
    if content_fills_image and already_correct_size:
        return False, "already ok"
    
    # Need to process
    # Crop to content
    if not content_fills_image:
        img = img.crop(bbox)
    
    # Resize maintaining aspect ratio, center on transparent bg
    if img.width != img.height:
        max_dim = max(img.width, img.height)
        scale = target_size / max_dim
        new_w = int(img.width * scale)
        new_h = int(img.height * scale)
        img = img.resize((new_w, new_h), Image.LANCZOS)
        
        # Center on transparent background
        result = Image.new('RGBA', (target_size, target_size), (0, 0, 0, 0))
        paste_x = (target_size - new_w) // 2
        paste_y = (target_size - new_h) // 2
        result.paste(img, (paste_x, paste_y))
        img = result
    else:
        img = img.resize((target_size, target_size), Image.LANCZOS)
    
    # Build description
    changes = []
    if not content_fills_image:
        changes.append(f"trim {content_w}x{content_h}")
    if not already_correct_size:
        changes.append(f"{original_size[0]}x{original_size[1]}")
    changes.append(f"-> {target_size}x{target_size}")
    desc = " ".join(changes)
    
    if debug:
        print(f"  Original: {original_size}, mode: {original_mode}")
        print(f"  Content bbox: {bbox} ({content_w}x{content_h})")
        print(f"  Result: {desc}")
        # Save as ~filename_resized.png
        p = Path(png_path)
        debug_path = p.parent / f"~{p.stem}_resized.png"
        img.save(debug_path, 'PNG')
        print(f"  Saved: {debug_path}")
        return False, "debug"
    
    # Backup original before overwrite
    if backup_dir:
        backup_dir.mkdir(parents=True, exist_ok=True)
        # Use provided name or derive from path
        if backup_name:
            name = backup_name
        else:
            # For app icons: use parent folder name
            # For system icons: use filename
            p = Path(png_path)
            name = p.parent.name if p.name == "icon.png" else p.stem
        backup_path = backup_dir / f"{name}.png"
        if not backup_path.exists():  # Don't overwrite existing backup
            shutil.copy2(png_path, backup_path)
    
    # Save over original
    img.save(png_path, 'PNG')
    return True, desc


def process_all_icons(apps_dir, target_size=DEFAULT_SIZE, project_dir=None):
    """Process all icons in apps directory"""
    if not apps_dir.exists():
        print(f"=== Resize Icons: apps dir not found ===")
        return
    
    # Cleanup debug files first
    cleaned = cleanup_debug_files(apps_dir)
    
    # Backup directory (outside data/ so it won't go to FS)
    if project_dir:
        backup_dir = project_dir / "original_icons"
    else:
        backup_dir = apps_dir.parent.parent / "original_icons"
    
    processed = 0
    skipped = 0
    results = []
    
    # Process app icons
    for app_dir in sorted(apps_dir.iterdir()):
        if not app_dir.is_dir():
            continue
        
        icon_path = app_dir / "icon.png"
        if not icon_path.exists():
            continue
        
        app_name = app_dir.name
        file_size = icon_path.stat().st_size
        
        try:
            changed, reason = process_icon(icon_path, target_size, backup_dir=backup_dir)
            if changed:
                results.append(f"  {app_name}: {file_size//1024}KB -> {reason}")
                processed += 1
            else:
                skipped += 1
        except Exception as e:
            results.append(f"  {app_name}: FAILED - {e}")
    
    # Process system category icons
    system_icons_dir = apps_dir.parent / "system" / "resources" / "icons"
    system_backup_dir = backup_dir / "system" if backup_dir else None
    
    if system_icons_dir.exists():
        for icon_path in sorted(system_icons_dir.glob("*.png")):
            icon_name = icon_path.stem
            file_size = icon_path.stat().st_size
            
            try:
                changed, reason = process_icon(icon_path, target_size, backup_dir=system_backup_dir)
                if changed:
                    results.append(f"  [system] {icon_name}: {file_size//1024}KB -> {reason}")
                    processed += 1
                else:
                    skipped += 1
            except Exception as e:
                results.append(f"  [system] {icon_name}: FAILED - {e}")
    
    # Only print details if something happened
    if processed > 0 or cleaned > 0:
        print(f"=== Resize Icons ({target_size}x{target_size}) ===")
        if cleaned > 0:
            print(f"  Cleaned {cleaned} debug file(s)")
        for r in results:
            print(r)
        print(f"  Done: {processed} resized, {skipped} ok")
    else:
        print(f"=== Resize Icons: all {skipped} ok ===")


def debug_single(png_path, target_size=DEFAULT_SIZE):
    """Debug mode: analyze and create preview without modifying original"""
    print(f"=== Debug: {png_path} ===")
    
    p = Path(png_path)
    if not p.exists():
        print("  ERROR: File not found")
        return
    
    process_icon(png_path, target_size, debug=True)


def main():
    parser = argparse.ArgumentParser(description='Prepare app icons')
    parser.add_argument('-d', metavar='FILE', help='Debug: create ~file_resized.png preview')
    parser.add_argument('--size', type=int, default=DEFAULT_SIZE, 
                        help=f'Target size (default: {DEFAULT_SIZE})')
    
    args = parser.parse_args()
    
    if args.d:
        debug_single(args.d, args.size)
    else:
        script_dir = Path(__file__).parent
        project_dir = script_dir.parent
        apps_dir = project_dir / "data" / "apps"
        
        if not apps_dir.exists():
            apps_dir = Path("data/apps")
            project_dir = Path(".")
        
        process_all_icons(apps_dir, args.size, project_dir)


if __name__ == "__main__":
    main()


# === PlatformIO mode (runs for all platforms) ===
try:
    Import("env")
    project_dir = Path(env.subst("$PROJECT_DIR"))
    apps_dir = project_dir / "data" / "apps"
    
    # Always resize to 64x64 (master size on disk)
    process_all_icons(apps_dir, DEFAULT_SIZE, project_dir)
except NameError:
    pass  # Not running in PlatformIO
except Exception as e:
    print(f"resize_icons.py ERROR: {e}")