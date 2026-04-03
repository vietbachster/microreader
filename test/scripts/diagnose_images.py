"""
Deep-dive into specific image issues:
- Find images that are overflowing pages
- Find images with 0x0 that got through
- Check what the actual intrinsic sizes are
"""
import re
import os
import sys
import zipfile

TEST_DIR = os.path.dirname(os.path.abspath(__file__))
HTML_DIR = os.path.join(os.path.dirname(TEST_DIR), "output", "html")
BOOKS_DIR = os.path.join(TEST_DIR, "books")

def check_overflow_images(html_path):
    """Find images that extend beyond page bounds."""
    with open(html_path, 'r', encoding='utf-8') as f:
        html = f.read()
    
    page_h = 800
    padding = 20
    content_h = page_h - 2 * padding  # 760
    
    # Find all images with their positions and sizes
    for m in re.finditer(r'class="img-abs" style="left:(\d+)px;top:(\d+)px"[^>]*>\s*<img[^>]*width="(\d+)" height="(\d+)"', html, re.DOTALL):
        x, y, w, h = int(m.group(1)), int(m.group(2)), int(m.group(3)), int(m.group(4))
        bottom = y + h
        if bottom > page_h or h > content_h:
            print(f"  OVERFLOW: pos=({x},{y}) size={w}x{h} bottom={bottom} page_h={page_h}")

def check_xhtml_image_patterns():
    """Check how EPUBs reference images to understand patterns."""
    interesting = [
        ("Treasure_Island", "treasure-island.epub"),
        ("Eyes_of_the_Void", "Eyes of the Void.epub"),
        ("Dictators_Handbook", "The Dictators Handbook.epub"),
        ("Snow_Crash", "Snow Crash - Neal Stephenson.epub"),
        ("Der_totale_Rausch", "ohler.epub"),
    ]
    
    for label, epub_name in interesting:
        # Find the epub
        found = None
        for root, dirs, files in os.walk(BOOKS_DIR):
            for f in files:
                if f == epub_name or epub_name.lower().replace('.epub', '') in f.lower():
                    found = os.path.join(root, f)
                    break
        
        if not found:
            # Try other locations
            for alt_dir in [os.path.join(os.path.dirname(BOOKS_DIR), "books"), 
                           r"c:\Users\Patrick\Desktop\microreader\microreader\resources\books"]:
                if os.path.exists(alt_dir):
                    for f in os.listdir(alt_dir):
                        if epub_name.lower().replace('.epub', '') in f.lower():
                            found = os.path.join(alt_dir, f)
                            break
        
        if not found:
            print(f"\n--- {label}: NOT FOUND ({epub_name}) ---")
            continue
        
        print(f"\n--- {label}: {os.path.basename(found)} ---")
        try:
            z = zipfile.ZipFile(found)
        except Exception as e:
            print(f"  Error: {e}")
            continue
        
        xhtml_files = [f for f in z.namelist() if f.endswith(('.xhtml', '.html', '.htm'))]
        images = [f for f in z.namelist() if any(f.endswith(e) for e in ('.jpg', '.jpeg', '.png', '.gif'))]
        
        print(f"  XHTML files: {len(xhtml_files)}, Image files: {len(images)}")
        
        # Check CSS for image-related rules
        css_files = [f for f in z.namelist() if f.endswith('.css')]
        for cf in css_files:
            css = z.read(cf).decode('utf-8', errors='replace')
            # Find image/figure/illustration related rules
            for m in re.finditer(r'([.#][\w-]*(?:img|image|figure|illus|plate|photo|pic|cover|wrap)[^{]*)\{([^}]*)\}', css, re.IGNORECASE):
                print(f"  CSS: {m.group(1).strip()} {{ {m.group(2).strip()[:100]} }}")
        
        # Check first few XHTML files for image patterns
        img_count = 0
        for xf in xhtml_files[:10]:
            content = z.read(xf).decode('utf-8', errors='replace')
            for img_m in re.finditer(r'<img[^>]+>', content, re.IGNORECASE):
                tag = img_m.group()
                img_count += 1
                if img_count <= 5:
                    # Show context
                    idx = img_m.start()
                    before = content[max(0, idx-100):idx]
                    before_tags = re.findall(r'<[^>]+>', before)
                    parent = before_tags[-1] if before_tags else "?"
                    
                    has_w = 'width=' in tag
                    has_h = 'height=' in tag
                    src = re.search(r'src="([^"]*)"', tag)
                    print(f"  IMG #{img_count}: {'w+h' if has_w and has_h else 'NO_DIMS'} parent={parent[:60]} src={src.group(1)[:40] if src else '?'}")
        
        if img_count > 5:
            print(f"  ... and {img_count - 5} more images")
        
        z.close()

def check_relativity_gif_images():
    """Special check for Relativity book which has 225 image errors (likely GIF)."""
    epub_path = None
    for root, dirs, files in os.walk(BOOKS_DIR):
        for f in files:
            if 'relativity' in f.lower():
                epub_path = os.path.join(root, f)
                break
    
    if not epub_path:
        print("\nRelativity EPUB not found")
        return
    
    print(f"\n--- RELATIVITY IMAGE FORMAT CHECK ---")
    z = zipfile.ZipFile(epub_path)
    images = [f for f in z.namelist() if any(f.endswith(e) for e in ('.jpg', '.jpeg', '.png', '.gif', '.svg'))]
    
    by_ext = {}
    for img in images:
        ext = os.path.splitext(img)[1].lower()
        by_ext.setdefault(ext, []).append(img)
    
    for ext, files in sorted(by_ext.items()):
        print(f"  {ext}: {len(files)} files")
        if files:
            # Check magic bytes of first file
            data = z.read(files[0])
            magic = data[:4].hex()
            print(f"    First file: {files[0][:60]} magic=0x{magic} size={len(data)}")
    
    z.close()

if __name__ == '__main__':
    print("=== OVERFLOW IMAGE ANALYSIS ===")
    
    # Check specific problematic files
    for name in ['toc_test_Treasure_Island.html', 'toc_test_Les_Misrables.html', 
                 'Der_totale_Rausch.html', 'Eyes_of_the_Void_9780316705882.html']:
        path = os.path.join(HTML_DIR, name)
        if os.path.exists(path):
            print(f"\n{name}:")
            check_overflow_images(path)
    
    print("\n=== EPUB IMAGE PATTERNS ===")
    check_xhtml_image_patterns()
    
    print("\n=== RELATIVITY GIF CHECK ===")
    check_relativity_gif_images()
