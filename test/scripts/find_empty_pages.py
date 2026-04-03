"""Find empty pages and analyze their context."""
import re, os

HTML_DIR = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "output", "html")

def find_empty_pages(html_path):
    with open(html_path, 'r', encoding='utf-8') as f:
        html = f.read()
    
    # Split into pages
    pages = re.findall(r'<div class="page">(.*?)</div>\s*(?=<div class="page(?:-break)?"|</div>\s*<!--)', html, re.DOTALL)
    if not pages:
        pages = re.findall(r'<div class="page">(.*?)</div>\s*(?=<div |</div>)', html, re.DOTALL)
    
    # Find page divs including their surrounding context
    page_starts = [(m.start(), m.end()) for m in re.finditer(r'<div class="page">', html)]
    
    for idx, start_pos in enumerate(page_starts):
        page_start = start_pos[1]
        # Find end of this page div
        depth = 1
        pos = page_start
        while depth > 0 and pos < len(html):
            next_open = html.find('<div', pos)
            next_close = html.find('</div>', pos)
            if next_close == -1:
                break
            if next_open != -1 and next_open < next_close:
                depth += 1
                pos = next_open + 4
            else:
                depth -= 1
                if depth == 0:
                    page_end = next_close
                    break
                pos = next_close + 6
        else:
            continue
        
        content = html[page_start:page_end].strip()
        
        # Check if page is empty (no line/img-abs/hr-abs content)
        has_line = 'class="line"' in content
        has_img = 'class="img-abs"' in content
        has_hr = 'class="hr-abs"' in content
        
        if not has_line and not has_img and not has_hr:
            # Get surrounding context - find page break before
            before = html[max(0, start_pos[0]-200):start_pos[0]]
            after = html[page_end:min(len(html), page_end+200)]
            
            # Find page number
            page_break = re.findall(r'page (\d+)', before)
            page_num = page_break[-1] if page_break else f"#{idx+1}"
            
            # Show preceding page's last few items
            if idx > 0:
                prev_start = page_starts[idx-1][1]
                prev_content = html[prev_start:start_pos[0]]
                prev_items = re.findall(r'class="(?:line|img-abs|hr-abs)"[^>]*>', prev_content)
                last_items = prev_items[-3:] if prev_items else ['(none)']
            else:
                last_items = ['(first page)']
            
            print(f"  Empty page {page_num} (page idx {idx})")
            print(f"    Preceding page last items: {last_items}")
            print(f"    Content: '{content[:100]}'")

for name in sorted(os.listdir(HTML_DIR)):
    if not name.endswith('.html'):
        continue
    path = os.path.join(HTML_DIR, name)
    with open(path, 'r', encoding='utf-8') as f:
        html = f.read()
    
    # Quick check: find pages with no positioned content
    page_pattern = re.compile(r'<div class="page">\s*</div>', re.DOTALL)
    empties = page_pattern.findall(html)
    if empties:
        print(f"\n{name}: {len(empties)} empty pages")
        find_empty_pages(path)
