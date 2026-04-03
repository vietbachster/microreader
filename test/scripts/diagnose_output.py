"""
Diagnose rendering issues in exported HTML files.
Checks for image overlap, zero-dimension images, content outside page bounds,
empty pages, and other layout problems.
"""
import re
import os
import sys

HTML_DIR = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "output", "html")

def parse_pages(html):
    """Extract page divs and their contents."""
    pages = []
    # Find all <div class="page"> blocks
    page_pattern = re.compile(r'<div class="page">(.*?)</div>\s*(?=<div class="page"|<div class="page-break"|</div>\s*<div class="chapter"|</div>\s*<div class="stats")', re.DOTALL)
    for m in page_pattern.finditer(html):
        page_html = m.group(1)
        items = []
        
        # Find text lines
        for line_m in re.finditer(r'class="line" style="left:(\d+)px;top:(\d+)px"', page_html):
            items.append(('text', int(line_m.group(1)), int(line_m.group(2)), 0, 16))
        
        # Find images
        for img_m in re.finditer(r'class="img-abs" style="left:(\d+)px;top:(\d+)px".*?width="(\d+)" height="(\d+)"', page_html, re.DOTALL):
            items.append(('image', int(img_m.group(1)), int(img_m.group(2)), int(img_m.group(3)), int(img_m.group(4))))
        
        # Find image errors
        for err_m in re.finditer(r'class="img-abs" style="left:(\d+)px;top:(\d+)px;width:(\d+)px;height:(\d+)px', page_html):
            items.append(('img_error', int(err_m.group(1)), int(err_m.group(2)), int(err_m.group(3)), int(err_m.group(4))))
        
        pages.append(items)
    return pages

def check_file(path):
    """Check a single HTML file for layout issues."""
    with open(path, 'r', encoding='utf-8') as f:
        html = f.read()
    
    name = os.path.basename(path)
    issues = []
    
    # Extract page dimensions from CSS
    page_w = 600
    page_h = 800
    padding = 20
    m = re.search(r'\.page \{ position: relative; width: (\d+)px; height: (\d+)px', html)
    if m:
        page_w = int(m.group(1))
        page_h = int(m.group(2))
    
    content_w = page_w - 2 * padding
    content_h = page_h - 2 * padding
    
    # Check for zero-dimension images
    zero_imgs = re.findall(r'width="0" height="0"', html)
    if zero_imgs:
        issues.append(f"  {len(zero_imgs)} images with 0x0 dimensions")
    
    # Check for image errors
    img_errors = html.count('Image error')
    if img_errors:
        issues.append(f"  {img_errors} image decode errors")
    
    # Parse pages and check layout
    pages = parse_pages(html)
    
    overlap_count = 0
    out_of_bounds_count = 0
    empty_page_count = 0
    very_tall_img_count = 0
    
    for pi, items in enumerate(pages):
        if not items:
            empty_page_count += 1
            continue
        
        images_on_page = [it for it in items if it[0] in ('image', 'img_error')]
        
        for it in items:
            kind, x, y, w, h = it
            # Check bounds
            if kind == 'image' or kind == 'img_error':
                bottom = y + h
                right = x + w
                if bottom > page_h:
                    out_of_bounds_count += 1
                if right > page_w:
                    out_of_bounds_count += 1
                if h > page_h:
                    very_tall_img_count += 1
        
        # Check for overlapping elements
        for i in range(len(items)):
            for j in range(i + 1, len(items)):
                _, x1, y1, w1, h1 = items[i]
                _, x2, y2, w2, h2 = items[j]
                if items[i][0] == 'text':
                    h1 = 16  # approximate line height
                if items[j][0] == 'text':
                    h2 = 16
                # Check vertical overlap
                if y1 < y2 + h2 and y2 < y1 + h1:
                    # Only flag image-image or image-text overlaps
                    if items[i][0] != 'text' or items[j][0] != 'text':
                        overlap_count += 1
    
    if overlap_count:
        issues.append(f"  {overlap_count} element overlaps detected")
    if out_of_bounds_count:
        issues.append(f"  {out_of_bounds_count} elements extend beyond page bounds")
    if empty_page_count:
        issues.append(f"  {empty_page_count} empty pages")
    if very_tall_img_count:
        issues.append(f"  {very_tall_img_count} images taller than content area")
    
    # Count stats
    total_pages = len(pages)
    total_images = len(re.findall(r'class="img-abs"', html))
    total_lines = len(re.findall(r'class="line"', html))
    
    return {
        'name': name,
        'pages': total_pages,
        'images': total_images,
        'lines': total_lines,
        'issues': issues,
    }

def main():
    if not os.path.exists(HTML_DIR):
        print(f"Output dir not found: {HTML_DIR}")
        sys.exit(1)
    
    files = sorted(f for f in os.listdir(HTML_DIR) if f.endswith('.html'))
    print(f"Checking {len(files)} HTML files in {HTML_DIR}\n")
    
    total_issues = 0
    for fname in files:
        path = os.path.join(HTML_DIR, fname)
        result = check_file(path)
        
        status = "OK" if not result['issues'] else "ISSUES"
        print(f"[{status:6s}] {result['name'][:65]:65s} pages={result['pages']:4d} imgs={result['images']:3d}")
        for issue in result['issues']:
            print(f"         {issue}")
            total_issues += 1
    
    print(f"\nTotal files: {len(files)}, Total issues: {total_issues}")

if __name__ == '__main__':
    main()
