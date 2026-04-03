"""
Analyze XHTML files from EPUBs to discover formatting patterns we should support.
Looks at HTML elements, CSS classes, inline styles, and structural patterns.
"""
import re
import os
import zipfile
from collections import Counter

BOOKS_DIR = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "books")

def analyze_epub(epub_path, label):
    try:
        z = zipfile.ZipFile(epub_path)
    except Exception as e:
        print(f"  Error: {e}")
        return {}
    
    results = {
        'elements': Counter(),
        'css_props': Counter(),
        'patterns': [],
        'svg_count': 0,
        'table_count': 0,
        'list_count': 0,
        'blockquote_count': 0,
        'heading_count': Counter(),
        'inline_styles': Counter(),
    }
    
    xhtml_files = [f for f in z.namelist() if f.endswith(('.xhtml', '.html', '.htm'))]
    
    for xf in xhtml_files:
        try:
            content = z.read(xf).decode('utf-8', errors='replace')
        except:
            continue
        
        # Count interesting elements
        for tag in ['table', 'tr', 'td', 'th', 'ul', 'ol', 'li', 'blockquote', 
                     'pre', 'code', 'sup', 'sub', 'br', 'svg', 'figure', 'figcaption',
                     'aside', 'section', 'article', 'nav', 'header', 'footer',
                     'span', 'div', 'p', 'a', 'img', 'image']:
            count = len(re.findall(rf'<{tag}[\s>/]', content, re.IGNORECASE))
            if count:
                results['elements'][tag] += count
        
        # Count heading levels
        for level in range(1, 7):
            count = len(re.findall(rf'<h{level}[\s>]', content, re.IGNORECASE))
            if count:
                results['heading_count'][f'h{level}'] += count
        
        # Look for interesting CSS patterns in style attributes
        for m in re.finditer(r'style="([^"]*)"', content):
            style = m.group(1)
            for prop in ['text-align', 'font-size', 'margin', 'padding', 'text-indent',
                         'border', 'display', 'float', 'width', 'height', 'color',
                         'background', 'text-decoration', 'letter-spacing', 'line-height',
                         'text-transform', 'vertical-align', 'page-break']:
                if prop in style:
                    results['inline_styles'][prop] += 1
        
        # Check for text-align center patterns (common in poetry, headings)
        center_blocks = re.findall(r'text-align:\s*center', content)
        if center_blocks:
            results['patterns'].append(f'{xf}: {len(center_blocks)} centered blocks')
        
        # Check for indentation patterns
        indent_blocks = re.findall(r'(?:margin-left|text-indent|padding-left)\s*:\s*(\d+)', content)
        if indent_blocks:
            results['inline_styles']['indentation'] += len(indent_blocks)
        
        # Check for page-break directives
        pbreaks = re.findall(r'page-break-(?:before|after)\s*:\s*\w+', content)
        if pbreaks:
            results['patterns'].append(f'{xf}: {len(pbreaks)} page breaks')
    
    # Collect CSS files for analysis
    css_files = [f for f in z.namelist() if f.endswith('.css')]
    for cf in css_files:
        try:
            css = z.read(cf).decode('utf-8', errors='replace')
        except:
            continue
        
        # Count interesting CSS properties
        for prop in ['text-align', 'text-indent', 'margin-left', 'font-variant',
                     'text-transform', 'letter-spacing', 'float', 'clear',
                     'page-break-before', 'page-break-after', 'display',
                     'border', 'text-decoration', 'font-size', 'line-height']:
            count = len(re.findall(rf'{prop}\s*:', css))
            if count:
                results['css_props'][prop] += count
    
    z.close()
    return results

def main():
    all_elements = Counter()
    all_inline = Counter()
    all_css = Counter()
    all_headings = Counter()
    
    # Scan all book directories
    book_dirs = []
    workspace = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
    for rel in ['microreader2/test/books/gutenberg', 'microreader2/test/books/other',
                'microreader/resources/books', 'TrustyReader/sd']:
        d = os.path.join(workspace, rel)
        if os.path.exists(d):
            book_dirs.append(d)
    
    books_analyzed = 0
    for bdir in book_dirs:
        for f in sorted(os.listdir(bdir)):
            if not f.endswith('.epub'):
                continue
            path = os.path.join(bdir, f)
            label = f.replace('.epub', '')
            results = analyze_epub(path, label)
            if not results:
                continue
            books_analyzed += 1
            
            all_elements += results['elements']
            all_inline += results['inline_styles']
            all_css += results['css_props']
            all_headings += results['heading_count']
    
    print(f"Analyzed {books_analyzed} books\n")
    
    print("=== HTML ELEMENTS ACROSS ALL BOOKS ===")
    for elem, count in all_elements.most_common():
        print(f"  <{elem}>: {count}")
    
    print(f"\n=== HEADING LEVELS ===")
    for h, count in sorted(all_headings.items()):
        print(f"  <{h}>: {count}")
    
    print(f"\n=== INLINE STYLES ===")
    for prop, count in all_inline.most_common():
        print(f"  {prop}: {count}")
    
    print(f"\n=== CSS PROPERTIES ===")
    for prop, count in all_css.most_common():
        print(f"  {prop}: {count}")
    
    # Identify unsupported patterns
    print(f"\n=== FORMATTING GAPS (used in EPUBs but potentially unsupported) ===")
    unsupported = []
    if all_elements.get('table', 0):
        unsupported.append(f"Tables: {all_elements['table']} tables across all books")
    if all_elements.get('ul', 0) or all_elements.get('ol', 0):
        unsupported.append(f"Lists: {all_elements.get('ul', 0)} unordered + {all_elements.get('ol', 0)} ordered")
    if all_elements.get('blockquote', 0):
        unsupported.append(f"Blockquotes: {all_elements['blockquote']}")
    if all_elements.get('pre', 0):
        unsupported.append(f"Preformatted: {all_elements['pre']}")
    if all_elements.get('sup', 0) or all_elements.get('sub', 0):
        unsupported.append(f"Super/subscript: {all_elements.get('sup', 0)} sup + {all_elements.get('sub', 0)} sub")
    if all_elements.get('figure', 0):
        unsupported.append(f"Figures: {all_elements['figure']}")
    if all_elements.get('svg', 0):
        unsupported.append(f"SVG: {all_elements['svg']}")
    if all_inline.get('text-indent', 0) or all_css.get('text-indent', 0):
        unsupported.append(f"Text indent: {all_inline.get('text-indent', 0)} inline + {all_css.get('text-indent', 0)} CSS")
    if all_css.get('text-transform', 0):
        unsupported.append(f"Text transform (uppercase etc): {all_css['text-transform']}")
    if all_css.get('font-variant', 0):
        unsupported.append(f"Font variant (small-caps etc): {all_css['font-variant']}")
    if all_css.get('letter-spacing', 0):
        unsupported.append(f"Letter spacing: {all_css['letter-spacing']}")
    
    for u in unsupported:
        print(f"  - {u}")

if __name__ == '__main__':
    main()
