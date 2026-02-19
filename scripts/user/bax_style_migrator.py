#!/usr/bin/env python3
"""
BAX Style Migrator v1.0

Migrates inline style attributes to CSS-like <style> section.
Extracts common patterns and creates reusable classes.

Usage:
    python bax_style_migrator.py input.bax [output.bax]
"""

import re
import sys
from collections import defaultdict
from dataclasses import dataclass
from typing import Dict, List, Set, Tuple, Optional

# Attributes that can be extracted to CSS (style + repeating size)
STYLE_ATTRS = {'bgcolor', 'color', 'font', 'radius', 'text-align', 'text-valign', 'w', 'h'}

# Attributes that stay inline (position/behavior)
LAYOUT_ATTRS = {'x', 'y', 'align', 'valign', 'visible', 'bind', 'id', 
                'onclick', 'onhold', 'onchange', 'onenter', 'onblur', 'href',
                'min', 'max', 'placeholder', 'password', 'default', 'interval',
                'call', 'src', 'icon', 'iconsize', 'orientation', 'indicator',
                'language', 'name', 'z-index', 'ondraw', 'ontap', 'version', 
                'category', 'os'}


def merge_text_align(attrs: Dict[str, str]) -> Dict[str, str]:
    """Merge text-align + text-valign into compound text-align: 'h v'"""
    if 'text-align' in attrs and 'text-valign' in attrs:
        attrs = dict(attrs)
        h = attrs.pop('text-align')
        v = attrs.pop('text-valign')
        attrs['text-align'] = f'{h} {v}'
    return attrs


@dataclass
class StyleCombo:
    """A combination of style attributes"""
    attrs: Dict[str, str]
    
    def __hash__(self):
        return hash(tuple(sorted(self.attrs.items())))
    
    def __eq__(self, other):
        return self.attrs == other.attrs
    
    def to_css(self) -> str:
        """Convert to CSS-like syntax"""
        name_map = {'w': 'width', 'h': 'height'}
        merged = merge_text_align(self.attrs)
        parts = []
        for k, v in sorted(merged.items()):
            css_name = name_map.get(k, k)
            parts.append(f'{css_name}: {v};')
        return ' '.join(parts)
    
    def is_empty(self) -> bool:
        return len(self.attrs) == 0


def parse_tag(tag_str: str) -> Tuple[str, Dict[str, str], str]:
    """
    Parse a tag string into (tag_name, attributes, rest)
    Returns tag name, dict of attributes, and any content after >
    """
    # Match tag name
    m = re.match(r'<(\w+)', tag_str)
    if not m:
        return ('', {}, tag_str)
    
    tag_name = m.group(1)
    rest = tag_str[m.end():]
    
    # Parse attributes
    attrs = {}
    attr_pattern = r'\s+(\S+?)=["\']([^"\']*)["\']'
    
    for match in re.finditer(attr_pattern, rest):
        attr_name = match.group(1)
        attr_value = match.group(2)
        attrs[attr_name] = attr_value
    
    return (tag_name, attrs, rest)


def extract_style_attrs(attrs: Dict[str, str]) -> Tuple[Dict[str, str], Dict[str, str]]:
    """
    Split attributes into style attrs and other attrs
    """
    style = {}
    other = {}
    
    for k, v in attrs.items():
        if k in STYLE_ATTRS:
            style[k] = v
        else:
            other[k] = v
    
    return (style, other)


def normalize_color(color: str) -> str:
    """Normalize color to lowercase"""
    return color.lower()


def generate_class_name(combo: StyleCombo, index: int, all_combos: Dict['StyleCombo', List[int]]) -> str:
    """Generate semantic class name based on attributes"""
    attrs = combo.attrs
    parts = []
    
    w = attrs.get('w', '')
    h = attrs.get('h', '')
    bg = attrs.get('bgcolor', '')
    bg = bg.lower() if '{' not in bg else bg
    color = attrs.get('color', '')
    color = color.lower() if '{' not in color else color
    font = attrs.get('font', '')
    
    # 1. Определяем базовый тип
    if 'radius' in attrs and attrs['radius'] == '0':
        parts.append('cell')
    elif h in ('26', '28', '30') and w in ('11%', '12%'):
        # Маленькие кнопки - клавиши
        parts.append('key')
    elif h and w and not font:
        # Кнопка/ячейка без текстового стиля
        if '%' in str(h) and '%' in str(w):
            parts.append('btn')
        else:
            parts.append('box')
    elif font:
        parts.append('text')
    elif bg and not color and not font:
        parts.append('bg')
    else:
        parts.append('item')
    
    # 2. Добавляем модификаторы (кроме key — для них по умолчанию)
    if parts[0] != 'key':
        if bg:
            if bg in ('#111', '#111111', '#000', '#000000'):
                parts.append('dark')
            elif bg in ('#505050', '#555', '#666'):
                parts.append('gray')
            elif bg in ('#a0a0a0', '#aaa', '#999'):
                parts.append('light')
            elif '#ff9' in bg or '#f90' in bg or '#ff5' in bg:
                parts.append('orange')
            elif '#ff0000' in bg or '#f00' in bg or '#c62' in bg:
                parts.append('red')
            elif '#2e7' in bg or '#4caf' in bg or '#0f0' in bg:
                parts.append('green')
            elif '#15' in bg or '#06f' in bg or '#00f' in bg:
                parts.append('blue')
    
    # По цвету текста
    if color:
        if color in ('#888', '#888888', '#666', '#666666', '#aaa'):
            parts.append('muted')
    
    # По размеру шрифта
    if font:
        if font in ('48', '72'):
            parts.append('lg')
        elif font == '32':
            parts.append('md')
    
    name = '-'.join(parts) if len(parts) > 1 else (parts[0] if parts else f's{index}')
    
    return name


def analyze_file(content: str) -> Tuple[Dict[StyleCombo, List[int]], Dict[str, Dict[str, str]]]:
    """
    Analyze file and find:
    1. Tag base styles (90%+ coverage) 
    2. Style combinations (excluding tag-covered attrs)
    """
    tag_attrs = defaultdict(lambda: defaultdict(int))  # tag -> {attr=val -> count}
    tag_counts = defaultdict(int)  # tag -> total count
    tag_elements = defaultdict(list)  # tag -> [(line, tag_str, attrs), ...]
    
    tag_pattern = r'<(label|button|page|input|slider|switch|canvas|image)\s+[^>]*>'
    
    # First pass: collect all elements and count attr frequencies
    for i, line in enumerate(content.split('\n'), 1):
        for match in re.finditer(tag_pattern, line):
            tag_str = match.group(0)
            tag_name, attrs, _ = parse_tag(tag_str)
            
            tag_counts[tag_name] += 1
            tag_elements[tag_name].append((i, tag_str, attrs))
            
            # Track per-tag attribute frequency
            for attr, val in attrs.items():
                if attr in STYLE_ATTRS:
                    norm_val = val.lower() if attr in ('bgcolor', 'color') and '{' not in val else val
                    key = f"{attr}={norm_val}"
                    tag_attrs[tag_name][key] += 1
    
    # Determine tag base styles (90%+ coverage)
    tag_base_styles = {}
    for tag, attr_counts in tag_attrs.items():
        total = tag_counts[tag]
        if total < 3:
            continue
        
        base_style = {}
        for attr_val, count in attr_counts.items():
            if count / total >= 0.90:
                attr, val = attr_val.split('=', 1)
                base_style[attr] = val
        
        if base_style:
            tag_base_styles[tag] = base_style
    
    # Second pass: build combos EXCLUDING tag-covered attrs
    combos = defaultdict(list)
    
    for tag_name, elements in tag_elements.items():
        tag_base = tag_base_styles.get(tag_name, {})
        
        for line_num, tag_str, attrs in elements:
            style_attrs, _ = extract_style_attrs(attrs)
            
            # Remove attrs covered by tag selector
            filtered = {}
            for k, v in style_attrs.items():
                norm_v = v.lower() if k in ('bgcolor', 'color') and '{' not in v else v
                if tag_base.get(k) != norm_v:
                    filtered[k] = norm_v
            
            if filtered:
                combo = StyleCombo(filtered)
                combos[combo].append(line_num)
    
    return combos, tag_base_styles


def migrate_content(content: str, class_map: Dict[StyleCombo, str], tag_styles: Dict[str, Dict[str, str]] = None) -> str:
    """
    Migrate content: 
    - Replace inline styles with class references
    - Remove attributes covered by tag selectors
    """
    lines = content.split('\n')
    result = []
    
    tag_pattern = r'<(label|button|page|input|slider|switch|canvas|image)\s+[^>]*>'
    
    for line in lines:
        new_line = line
        
        for match in re.finditer(tag_pattern, line):
            tag_str = match.group(0)
            tag_name, attrs, _ = parse_tag(tag_str)
            
            style_attrs, other_attrs = extract_style_attrs(attrs)
            
            # Get tag base style
            tag_base = tag_styles.get(tag_name, {}) if tag_styles else {}
            
            # Remove attrs covered by tag selector
            filtered_style = {}
            for k, v in style_attrs.items():
                norm_v = v.lower() if k in ('bgcolor', 'color') and '{' not in v else v
                if tag_base.get(k) != norm_v:
                    filtered_style[k] = norm_v
            
            if filtered_style:
                combo = StyleCombo(filtered_style)
                
                if combo in class_map:
                    class_name = class_map[combo]
                    
                    if 'class' in other_attrs:
                        other_attrs['class'] = f"{other_attrs['class']} {class_name}"
                    else:
                        other_attrs['class'] = class_name
                    
                    # Rebuild tag without style attrs (they're in class now)
                    new_tag = f'<{tag_name}'
                    for k, v in other_attrs.items():
                        new_tag += f' {k}="{v}"'
                    
                    if tag_str.rstrip().endswith('/>'):
                        new_tag += '/>'
                    else:
                        new_tag += '>'
                    
                    new_line = new_line.replace(tag_str, new_tag)
                else:
                    # No class match - keep remaining style attrs inline
                    new_tag = f'<{tag_name}'
                    for k, v in other_attrs.items():
                        new_tag += f' {k}="{v}"'
                    for k, v in filtered_style.items():
                        new_tag += f' {k}="{v}"'
                    
                    if tag_str.rstrip().endswith('/>'):
                        new_tag += '/>'
                    else:
                        new_tag += '>'
                    
                    new_line = new_line.replace(tag_str, new_tag)
            
            elif tag_base:
                # All style attrs covered by tag selector - remove them
                new_tag = f'<{tag_name}'
                for k, v in other_attrs.items():
                    new_tag += f' {k}="{v}"'
                
                if tag_str.rstrip().endswith('/>'):
                    new_tag += '/>'
                else:
                    new_tag += '>'
                
                new_line = new_line.replace(tag_str, new_tag)
        
        result.append(new_line)
    
    return '\n'.join(result)


def generate_style_section(class_map: Dict[StyleCombo, str], tag_styles: Dict[str, Dict[str, str]] = None, indent: str = '  ') -> str:
    """Generate <style> section with tag selectors and class selectors"""
    name_map = {'w': 'width', 'h': 'height'}
    lines = [f'{indent}<style>']

    # Tag selectors first (base styles)
    if tag_styles:
        for tag, attrs in sorted(tag_styles.items()):
            merged = merge_text_align(attrs)
            css_parts = [f'{name_map.get(k, k)}: {v};' for k, v in sorted(merged.items())]
            css = ' '.join(css_parts)
            lines.append(f'{indent}  {tag} {{ {css} }}')

        if class_map:
            lines.append('')  # separator

    # Class selectors
    sorted_items = sorted(class_map.items(), key=lambda x: x[1])

    for combo, class_name in sorted_items:
        css = combo.to_css()
        lines.append(f'{indent}  .{class_name} {{ {css} }}')

    lines.append(f'{indent}</style>')
    return '\n'.join(lines)


def normalize_block_spacing(content: str) -> str:
    """Ensure exactly one blank line between top-level blocks inside <app>"""
    lines = content.split('\n')
    result = []
    prev_blank = False
    
    for line in lines:
        is_blank = line.strip() == ''
        if is_blank and prev_blank:
            continue  # skip consecutive blank lines
        result.append(line if not is_blank else '')
        prev_blank = is_blank
    
    return '\n'.join(result)


def migrate_file(input_path: str, output_path: Optional[str] = None, min_occurrences: int = 3, dry_run: bool = False):
    """
    Main migration function
    """
    with open(input_path, 'r', encoding='utf-8') as f:
        content = f.read()
    
    print(f"=== Analyzing {input_path} ===\n")
    
    # Check for existing <style> block
    existing_style = re.search(r'<style>.*?</style>', content, re.DOTALL)
    if existing_style:
        print("⚠ File already has <style> block - will merge\n")
    
    # Analyze
    combos, tag_base_styles = analyze_file(content)
    
    # Show tag selector candidates
    if tag_base_styles:
        print("=== Tag Selectors (90%+ coverage) ===\n")
        for tag, attrs in sorted(tag_base_styles.items()):
            merged = merge_text_align(attrs)
            attrs_str = '; '.join(f'{k}: {v}' for k, v in sorted(merged.items()))
            print(f"  {tag} {{ {attrs_str} }}")
        print()
    
    # Filter by occurrences
    frequent_combos = {k: v for k, v in combos.items() if len(v) >= min_occurrences}
    
    print(f"Total unique style combinations: {len(combos)}")
    print(f"Combinations with {min_occurrences}+ uses: {len(frequent_combos)}")
    print()
    
    # Show analysis
    print("=== Style Combinations ===\n")
    sorted_combos = sorted(combos.items(), key=lambda x: -len(x[1]))
    
    for combo, lines in sorted_combos[:20]:
        count = len(lines)
        css = combo.to_css()
        marker = "✓" if count >= min_occurrences else " "
        print(f"  {marker} [{count:2d}x] {css}")
    
    if len(sorted_combos) > 20:
        print(f"  ... and {len(sorted_combos) - 20} more")
    
    print()
    
    # Generate class names
    class_map = {}
    used_names = set()
    
    for i, combo in enumerate(sorted(frequent_combos.keys(), key=lambda c: -len(frequent_combos[c]))):
        class_name = generate_class_name(combo, i, frequent_combos)
        
        # Ensure unique
        base_name = class_name
        counter = 2
        while class_name in used_names:
            class_name = f"{base_name}-{counter}"
            counter += 1
        
        used_names.add(class_name)
        class_map[combo] = class_name
    
    # Show generated classes
    if class_map:
        print("=== Generated Classes ===\n")
        for combo, class_name in sorted(class_map.items(), key=lambda x: x[1]):
            count = len(frequent_combos[combo])
            css = combo.to_css()
            print(f"  {class_name} [{count}x]")
            print(f"    {css}")
            print()
    
    if dry_run:
        print("=== Dry Run - no files written ===")
        return None
    
    # Migrate content
    new_content = migrate_content(content, class_map, tag_base_styles)
    
    # Handle style section
    if class_map or tag_base_styles:
        style_section = generate_style_section(class_map, tag_base_styles)
        
        if existing_style:
            # Merge with existing style block
            old_style = existing_style.group(0)
            # Extract content between <style> and </style>
            old_content = re.search(r'<style>(.*?)</style>', old_style, re.DOTALL).group(1)
            new_style_content = re.search(r'<style>(.*?)</style>', style_section, re.DOTALL).group(1)
            
            # Merge: new styles first, then old
            merged_style = f'<style>\n  /* Auto-generated */\n{new_style_content}\n  /* Original */\n{old_content}</style>'
            new_content = new_content.replace(old_style, merged_style)
        else:
            # Insert after </ui> tag with clean spacing
            ui_end = re.search(r'([ \t]*)</ui>', new_content)
            if ui_end:
                indent = ui_end.group(1)
                style_section = generate_style_section(class_map, tag_base_styles, indent)
                # Replace </ui>...\n with </ui>\n\n<style>...\n\n
                rest_after_ui = new_content[ui_end.end():]
                # Strip all leading whitespace/newlines, we'll re-add proper spacing
                rest_after_ui = re.sub(r'^[\s]*\n', '', rest_after_ui, count=0)
                rest_after_ui = rest_after_ui.lstrip('\n\r')
                # re-add indent if stripped
                if rest_after_ui and not rest_after_ui[0].isspace():
                    rest_after_ui = indent + rest_after_ui
                new_content = new_content[:ui_end.end()] + '\n\n' + style_section + '\n\n' + rest_after_ui
    
    # Stats
    original_size = len(content)

    # Normalize: exactly one blank line between top-level blocks
    new_content = normalize_block_spacing(new_content)

    new_size = len(new_content)
    
    print("=== Results ===\n")
    print(f"  Original size: {original_size} bytes")
    print(f"  New size:      {new_size} bytes")
    print(f"  Difference:    {new_size - original_size:+d} bytes ({(new_size/original_size - 1)*100:+.1f}%)")
    print(f"  Tag selectors: {len(tag_base_styles)}")
    print(f"  Classes created: {len(class_map)}")
    print(f"  Attributes migrated: {sum(len(v) for v in frequent_combos.values())}")
    
    # Output
    if output_path:
        with open(output_path, 'w', encoding='utf-8') as f:
            f.write(new_content)
        print(f"\n  Output written to: {output_path}")
    
    return new_content


if __name__ == '__main__':
    import argparse
    
    parser = argparse.ArgumentParser(description='BAX Style Migrator - extract inline styles to CSS')
    parser.add_argument('input', nargs='+', help='Input .bax file(s) or directory')
    parser.add_argument('-o', '--output', help='Output file (for single input) or directory (for batch)')
    parser.add_argument('-n', '--min-occurrences', type=int, default=3, help='Minimum occurrences for class extraction (default: 3)')
    parser.add_argument('--dry-run', action='store_true', help='Analyze only, do not write files')
    
    args = parser.parse_args()
    
    import os
    
    # Collect all input files
    input_files = []
    for inp in args.input:
        if os.path.isdir(inp):
            for root, dirs, files in os.walk(inp):
                for f in files:
                    if f.endswith('.bax'):
                        input_files.append(os.path.join(root, f))
        elif os.path.isfile(inp):
            input_files.append(inp)
        else:
            print(f"Warning: {inp} not found")
    
    if not input_files:
        print("No .bax files found")
        sys.exit(1)
    
    # Process files
    for input_file in input_files:
        if len(input_files) > 1:
            print(f"\n{'='*60}\n")
        
        if args.dry_run:
            migrate_file(input_file, None, args.min_occurrences, dry_run=True)
        elif args.output:
            if len(input_files) == 1:
                output_file = args.output
            else:
                # Batch mode: preserve relative structure
                basename = os.path.basename(input_file)
                output_file = os.path.join(args.output, basename)
                os.makedirs(os.path.dirname(output_file) or '.', exist_ok=True)
            migrate_file(input_file, output_file, args.min_occurrences)
        else:
            # In-place (suffix _migrated)
            base, ext = os.path.splitext(input_file)
            output_file = f"{base}_migrated{ext}"
            migrate_file(input_file, output_file, args.min_occurrences)