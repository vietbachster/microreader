"""
Analyze EPUB formatting patterns across test books.
Identifies CSS patterns, image usage, HTML element usage,
and unsupported features.
"""
import zipfile
import re
import os
import sys
from collections import Counter, defaultdict

BOOKS_DIR = os.path.join(os.path.dirname(__file__), "books")

def find_epubs(base_dir):
    """Find all .epub files recursively."""
    epubs = []
    for root, dirs, files in os.walk(base_dir):
        for f in files:
            if f.endswith('.epub'):
                epubs.append(os.path.join(root, f))
    return sorted(epubs)

def analyze_epub(path):
    """Analyze a single EPUB for formatting patterns."""
    result = {
        'name': os.path.basename(path),
        'xhtml_count': 0,
        'image_count': 0,
        'css_count': 0,
        'elements': Counter(),
        'css_properties': Counter(),
        'image_patterns': [],
        'has_svg': False,
        'has_table': False,
        'has_list': False,
        'has_footnotes': False,
        'image_in_text': 0,  # inline images (not standalone)
        'standalone_images': 0,
        'cover_images': 0,
        'errors': [],
    }
    
    try:
        z = zipfile.ZipFile(path)
    except Exception as e:
        result['errors'].append(str(e))
        return result
    
    all_files = z.namelist()
    xhtml_files = [f for f in all_files if f.endswith(('.xhtml', '.html', '.htm'))]
    image_files = [f for f in all_files if any(f.endswith(e) for e in ('.jpg', '.jpeg', '.png', '.gif', '.svg'))]
    css_files = [f for f in all_files if f.endswith('.css')]
    
    result['xhtml_count'] = len(xhtml_files)
    result['image_count'] = len(image_files)
    result['css_count'] = len(css_files)
    
    # Analyze CSS files
    for cf in css_files:
        try:
            css = z.read(cf).decode('utf-8', errors='replace')
            # Count CSS properties used
            for prop in re.findall(r'([\w-]+)\s*:', css):
                result['css_properties'][prop] += 1
        except:
            pass
    
    # Analyze XHTML files
    for xf in xhtml_files:
        try:
            content = z.read(xf).decode('utf-8', errors='replace')
        except:
            continue
        
        # Count HTML elements
        for tag in re.findall(r'<(\w+)[\s>]', content):
            result['elements'][tag.lower()] += 1
        
        # Check for SVG
        if '<svg' in content or '<SVG' in content:
            result['has_svg'] = True
        
        # Check for tables
        if '<table' in content.lower():
            result['has_table'] = True
        
        # Check for lists
        if '<ul' in content.lower() or '<ol' in content.lower():
            result['has_list'] = True
        
        # Check for footnotes
        if 'footnote' in content.lower() or 'endnote' in content.lower():
            result['has_footnotes'] = True
        
        # Analyze image usage patterns
        img_tags = re.findall(r'<img[^>]+>', content, re.IGNORECASE)
        svg_images = re.findall(r'<image[^>]+>', content, re.IGNORECASE)
        
        for img in img_tags + svg_images:
            # Check if image is wrapped in its own container (standalone)
            # vs. inline with text
            # Look for context around the image
            idx = content.find(img)
            if idx >= 0:
                before = content[max(0, idx-200):idx]
                after = content[idx+len(img):idx+len(img)+200]
                
                # Check for cover
                if 'cover' in before.lower() or 'cover' in img.lower():
                    result['cover_images'] += 1
                # Check if inside a paragraph with text
                elif re.search(r'[a-zA-Z]{2,}\s*$', before) or re.search(r'^\s*[a-zA-Z]{2,}', after):
                    result['image_in_text'] += 1
                else:
                    result['standalone_images'] += 1
                
                # Extract image patterns (class, style, width/height)
                classes = re.findall(r'class="([^"]*)"', img)
                styles = re.findall(r'style="([^"]*)"', img)
                widths = re.findall(r'width="([^"]*)"', img)
                heights = re.findall(r'height="([^"]*)"', img)
                
                pattern = {}
                if classes: pattern['class'] = classes[0]
                if styles: pattern['style'] = styles[0]
                if widths: pattern['width'] = widths[0]
                if heights: pattern['height'] = heights[0]
                if pattern:
                    result['image_patterns'].append(pattern)
    
    z.close()
    return result

def analyze_alice_detail(path):
    """Deep analysis of Alice in Wonderland formatting."""
    print("\n" + "="*80)
    print("DETAILED ANALYSIS: Alice's Adventures in Wonderland")
    print("="*80)
    
    z = zipfile.ZipFile(path)
    all_files = z.namelist()
    xhtml_files = [f for f in all_files if f.endswith(('.xhtml', '.html', '.htm'))]
    
    for xf in xhtml_files:
        content = z.read(xf).decode('utf-8', errors='replace')
        
        # Find float patterns
        floats = re.findall(r'class="([^"]*float[^"]*)"', content, re.IGNORECASE)
        if floats:
            print(f"\n  Float classes in {xf}:")
            for f in set(floats):
                print(f"    .{f}")
        
        # Find the initial-cap pattern (figleft)
        figleft_matches = re.findall(r'<div class="[^"]*fig[^"]*"[^>]*>.*?</div>', content, re.DOTALL | re.IGNORECASE)
        if figleft_matches:
            print(f"\n  Initial-cap patterns in {xf}:")
            for m in figleft_matches[:5]:
                print(f"    {m[:200]}")
        
        # Find image contexts
        img_tags = re.finditer(r'<img[^>]+>', content, re.IGNORECASE)
        for img in img_tags:
            idx = img.start()
            before = content[max(0,idx-300):idx]
            after = content[idx:idx+300]
            # Show surrounding tags
            print(f"\n  Image context in {xf}:")
            # Get just the last few tags before
            before_tags = re.findall(r'<[^>]+>', before)
            print(f"    Before: ...{' '.join(before_tags[-5:])}")
            print(f"    Image:  {img.group()[:150]}")
            after_tags = re.findall(r'<[^>]+>', after)
            print(f"    After:  {' '.join(after_tags[:5])}")
    z.close()

def analyze_image_problems(gutenberg_dir):
    """Specifically look at how images appear in EPUBs to understand the overlap/placement issues."""
    print("\n" + "="*80)
    print("IMAGE PLACEMENT ANALYSIS")
    print("="*80)
    
    # Pick a few illustrated books
    targets = [
        'alice-illustrated.epub',
        'Treasure_Island.epub', 
        'Elements_of_Euclid.epub',
    ]
    
    for name in targets:
        path = os.path.join(gutenberg_dir, name)
        if not os.path.exists(path):
            # Try to find it
            for f in os.listdir(gutenberg_dir):
                if name.lower().replace('.epub','') in f.lower():
                    path = os.path.join(gutenberg_dir, f)
                    break
        
        if not os.path.exists(path):
            print(f"\n  Skipping {name} (not found)")
            continue
        
        print(f"\n--- {name} ---")
        z = zipfile.ZipFile(path)
        xhtml_files = [f for f in z.namelist() if f.endswith(('.xhtml', '.html', '.htm'))]
        
        total_imgs = 0
        img_contexts = Counter()
        img_size_specs = []
        
        for xf in xhtml_files:
            content = z.read(xf).decode('utf-8', errors='replace')
            
            for img_match in re.finditer(r'<img[^>]+>', content, re.IGNORECASE):
                total_imgs += 1
                img_tag = img_match.group()
                idx = img_match.start()
                
                # Determine context
                before = content[max(0,idx-500):idx]
                after = content[idx+len(img_tag):idx+len(img_tag)+200]
                
                # What wraps the image?
                parent_tags = re.findall(r'<(\w+)[^>]*>', before)
                close_tags = re.findall(r'</(\w+)>', before)
                # Simple nesting
                open_stack = []
                for t in re.finditer(r'<(/?)(\w+)[^>]*>', before):
                    if t.group(1) == '/':
                        if open_stack and open_stack[-1] == t.group(2):
                            open_stack.pop()
                    else:
                        open_stack.append(t.group(2))
                
                parent = open_stack[-1] if open_stack else "?"
                img_contexts[parent] += 1
                
                # Check for width/height attributes
                w = re.search(r'width="(\d+)"', img_tag)
                h = re.search(r'height="(\d+)"', img_tag)
                if w or h:
                    img_size_specs.append((
                        int(w.group(1)) if w else None,
                        int(h.group(1)) if h else None
                    ))
                
                # Check for CSS width/height in style
                style = re.search(r'style="([^"]*)"', img_tag)
                if style:
                    sw = re.search(r'width:\s*(\d+)', style.group(1))
                    sh = re.search(r'height:\s*(\d+)', style.group(1))
                    if sw or sh:
                        img_size_specs.append((
                            int(sw.group(1)) if sw else None,
                            int(sh.group(1)) if sh else None
                        ))
        
        print(f"  Total images: {total_imgs}")
        print(f"  Parent element distribution: {dict(img_contexts.most_common(10))}")
        if img_size_specs:
            widths = [w for w, h in img_size_specs if w]
            heights = [h for w, h in img_size_specs if h]
            if widths:
                print(f"  Image widths: min={min(widths)}, max={max(widths)}, avg={sum(widths)//len(widths)}")
            if heights:
                print(f"  Image heights: min={min(heights)}, max={max(heights)}, avg={sum(heights)//len(heights)}")
        else:
            print(f"  No explicit size attributes on images")
        z.close()

def analyze_svg_images(gutenberg_dir):
    """Look for SVG image wrapping patterns."""
    print("\n" + "="*80)
    print("SVG IMAGE WRAPPING ANALYSIS")
    print("="*80)
    
    for epub_file in os.listdir(gutenberg_dir):
        if not epub_file.endswith('.epub'):
            continue
        path = os.path.join(gutenberg_dir, epub_file)
        try:
            z = zipfile.ZipFile(path)
        except:
            continue
        
        for xf in z.namelist():
            if not xf.endswith(('.xhtml', '.html', '.htm')):
                continue
            try:
                content = z.read(xf).decode('utf-8', errors='replace')
            except:
                continue
            
            if '<svg' in content.lower():
                # Show SVG patterns
                svg_blocks = re.findall(r'<svg[^>]*>.*?</svg>', content, re.DOTALL | re.IGNORECASE)
                if svg_blocks:
                    print(f"\n  {epub_file} / {xf}:")
                    for block in svg_blocks[:2]:
                        print(f"    {block[:300]}...")
        z.close()

def main():
    gutenberg_dir = os.path.join(BOOKS_DIR, "gutenberg")
    other_dir = os.path.join(BOOKS_DIR, "other")
    
    if not os.path.exists(gutenberg_dir):
        print(f"Gutenberg dir not found: {gutenberg_dir}")
        sys.exit(1)
    
    # 1. Overall statistics across all EPUBs
    print("="*80)
    print("EPUB FORMATTING ANALYSIS")
    print("="*80)
    
    all_epubs = find_epubs(BOOKS_DIR)
    print(f"\nTotal EPUBs found: {len(all_epubs)}")
    
    all_elements = Counter()
    all_css_props = Counter()
    books_with_images = 0
    books_with_svg = 0
    books_with_tables = 0
    books_with_lists = 0
    books_with_footnotes = 0
    total_images = 0
    total_inline_images = 0
    total_standalone_images = 0
    all_image_classes = Counter()
    
    for epub_path in all_epubs:
        r = analyze_epub(epub_path)
        all_elements.update(r['elements'])
        all_css_props.update(r['css_properties'])
        if r['image_count'] > 0:
            books_with_images += 1
        if r['has_svg']:
            books_with_svg += 1
        if r['has_table']:
            books_with_tables += 1
        if r['has_list']:
            books_with_lists += 1
        if r['has_footnotes']:
            books_with_footnotes += 1
        total_images += r['image_count']
        total_inline_images += r['image_in_text']
        total_standalone_images += r['standalone_images']
        for pat in r['image_patterns']:
            if 'class' in pat:
                all_image_classes[pat['class']] += 1
    
    print(f"\n--- Feature Usage Across {len(all_epubs)} EPUBs ---")
    print(f"  Books with images: {books_with_images}")
    print(f"  Books with SVG: {books_with_svg}")
    print(f"  Books with tables: {books_with_tables}")
    print(f"  Books with lists: {books_with_lists}")
    print(f"  Books with footnotes: {books_with_footnotes}")
    print(f"  Total image files: {total_images}")
    print(f"  Inline images (in text): {total_inline_images}")
    print(f"  Standalone images: {total_standalone_images}")
    
    print(f"\n--- Top 30 HTML Elements ---")
    for elem, count in all_elements.most_common(30):
        print(f"  <{elem}>: {count}")
    
    print(f"\n--- Top 30 CSS Properties ---")
    for prop, count in all_css_props.most_common(30):
        print(f"  {prop}: {count}")
    
    print(f"\n--- Image CSS Classes ---")
    for cls, count in all_image_classes.most_common(20):
        print(f"  .{cls}: {count}")
    
    # 2. Detailed Alice analysis
    alice_path = os.path.join(gutenberg_dir, "alice-illustrated.epub")
    if os.path.exists(alice_path):
        analyze_alice_detail(alice_path)
    
    # 3. Image placement analysis
    analyze_image_problems(gutenberg_dir)
    
    # 4. SVG analysis (limit output)
    analyze_svg_images(gutenberg_dir)
    
    # 5. Specific problem areas
    print("\n" + "="*80)
    print("PROBLEM PATTERN ANALYSIS")
    print("="*80)
    
    # Look for images that have no width/height (will be 0x0 in our parser)
    no_size_count = 0
    has_size_count = 0
    for epub_path in all_epubs:
        try:
            z = zipfile.ZipFile(epub_path)
        except:
            continue
        for xf in z.namelist():
            if not xf.endswith(('.xhtml', '.html', '.htm')):
                continue
            try:
                content = z.read(xf).decode('utf-8', errors='replace')
            except:
                continue
            for img in re.finditer(r'<img[^>]+>', content, re.IGNORECASE):
                tag = img.group()
                has_w = 'width=' in tag
                has_h = 'height=' in tag
                if has_w and has_h:
                    has_size_count += 1
                else:
                    no_size_count += 1
        z.close()
    
    print(f"\n  Images with explicit width+height: {has_size_count}")
    print(f"  Images WITHOUT explicit dimensions: {no_size_count}")
    print(f"  (Images without dimensions get 0x0 in our parser)")
    
    # Look for images wrapped in SVG viewBox (common Gutenberg pattern)
    svg_wrapped_count = 0
    for epub_path in find_epubs(gutenberg_dir):
        try:
            z = zipfile.ZipFile(epub_path)
        except:
            continue
        for xf in z.namelist():
            if not xf.endswith(('.xhtml', '.html', '.htm')):
                continue
            try:
                content = z.read(xf).decode('utf-8', errors='replace')
            except:
                continue
            if '<svg' in content.lower() and '<image' in content.lower():
                svg_wrapped_count += 1
        z.close()
    
    print(f"  XHTML files with SVG-wrapped images: {svg_wrapped_count}")
    
    print("\n" + "="*80)
    print("DONE")
    print("="*80)

if __name__ == '__main__':
    main()
