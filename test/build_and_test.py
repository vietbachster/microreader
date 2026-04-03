"""
Build, run all tests, and validate Alice export.
Single script to run everything.
"""
import subprocess
import sys
import os
import re

TEST_DIR = os.path.dirname(os.path.abspath(__file__))
BUILD_DIR = os.path.join(TEST_DIR, "build2")
EXE = os.path.join(BUILD_DIR, "Debug", "microreader_tests.exe")

def run(cmd, cwd=TEST_DIR, timeout=300):
    """Run a command and return (returncode, stdout)."""
    print(f"\n{'='*60}")
    print(f"  {' '.join(cmd) if isinstance(cmd, list) else cmd}")
    print(f"{'='*60}")
    result = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True, 
                          timeout=timeout, shell=isinstance(cmd, str))
    if result.stdout:
        print(result.stdout[-3000:] if len(result.stdout) > 3000 else result.stdout)
    if result.stderr:
        print("STDERR:", result.stderr[-2000:] if len(result.stderr) > 2000 else result.stderr)
    return result.returncode, result.stdout

def main():
    errors = []
    
    # 1. Build
    print("\n" + "#"*60)
    print("  STEP 1: BUILD")
    print("#"*60)
    rc, out = run(["cmake", "--build", "build2", "--config", "Debug"], cwd=TEST_DIR)
    if rc != 0:
        print("\n*** BUILD FAILED ***")
        sys.exit(1)
    print("BUILD OK")
    
    # 2. Run core unit tests (excluding slow book tests)
    print("\n" + "#"*60)
    print("  STEP 2: CORE UNIT TESTS")
    print("#"*60)
    rc, out = run([EXE, "--gtest_filter=CssParserTest.*:TextLayout.*:PageLayout.*:XhtmlBody.*:EpubPathResolve.*:EpubOpen.*:EpubParseChapter.*"])
    if rc != 0:
        errors.append("Core unit tests FAILED")
        print("\n*** CORE TESTS FAILED ***")
    else:
        # Count passed
        m = re.search(r'\[  PASSED  \] (\d+) test', out)
        count = m.group(1) if m else "?"
        print(f"\nCORE TESTS: {count} PASSED")
    
    # 3. Run all fixture tests (real EPUBs)
    print("\n" + "#"*60)
    print("  STEP 3: EPUB FIXTURE TESTS")
    print("#"*60)
    rc, out = run([EXE, "--gtest_filter=AllEpubs*"])
    if rc != 0:
        errors.append("Fixture tests FAILED")
        print("\n*** FIXTURE TESTS FAILED ***")
    else:
        m = re.search(r'\[  PASSED  \] (\d+) test', out)
        count = m.group(1) if m else "?"
        print(f"\nFIXTURE TESTS: {count} PASSED")

    # 4. Export Alice and check
    print("\n" + "#"*60)
    print("  STEP 4: EXPORT ALICE + VALIDATION")
    print("#"*60)
    rc, out = run([EXE, "--gtest_filter=HtmlExport.GutenbergPicks"])
    if rc != 0:
        errors.append("HTML Export FAILED")
        print("\n*** HTML EXPORT FAILED ***")
    else:
        print("HTML EXPORT OK")
    
    # 5. Check Alice first words
    print("\n" + "#"*60)
    print("  STEP 5: CHECK ALICE INITIAL CAPS")
    print("#"*60)
    
    alice_html = os.path.join(TEST_DIR, "output", "html",
        "Alices_Adventures_in_Wonderland__Illustrated_by_Arthur_Rackham._With_a_Proem_by_Austin_Dobson.html")
    
    if os.path.exists(alice_html):
        with open(alice_html, 'r', encoding='utf-8') as f:
            content = f.read()
        
        # Check chapters 2-12 for initial-cap merge
        initial_cap_ok = True
        for ch in range(2, 13):
            tag = f'id="ch{ch}"'
            idx = content.find(tag)
            if idx < 0:
                continue
            chunk = content[idx:idx+5000]
            words = re.findall(r'<span class="w[^"]*"[^>]*>([^<]+)</span>', chunk)
            first_20 = ' '.join(words[:20])
            print(f"  Chapter {ch:2d}: {first_20[:80]}")
            
            # Check for broken initial caps (space inside first word)
            if words:
                first_word = words[0]
                # Initial cap words should not be single uppercase letters followed by lowercase
                if len(first_word) == 1 and first_word.isupper() and len(words) > 1:
                    combined = first_word + words[1]
                    if combined.isalpha():
                        print(f"    *** BROKEN INITIAL CAP: '{first_word}' + '{words[1]}' should be '{combined}'")
                        initial_cap_ok = False
        
        if initial_cap_ok:
            print("\n  Initial caps: ALL OK")
        else:
            errors.append("Initial cap merge still broken")
        
        # Check for image dimensions (look for 0x0 images)
        img_items = re.findall(r'width="(\d+)" height="(\d+)"', content)
        zero_imgs = sum(1 for w, h in img_items if w == "0" or h == "0")
        total_imgs = len(img_items)
        print(f"\n  Images in HTML: {total_imgs} total, {zero_imgs} with 0-dimension")
        if zero_imgs > 0:
            errors.append(f"{zero_imgs} images still have 0-dimension")
    else:
        errors.append("Alice HTML not found")
        print("  Alice HTML file not found!")
    
    # 6. Check a few more books for image dimensions
    print("\n" + "#"*60)
    print("  STEP 6: IMAGE DIMENSION CHECK ACROSS BOOKS")
    print("#"*60)
    
    html_dir = os.path.join(TEST_DIR, "output", "html")
    if os.path.exists(html_dir):
        for html_file in sorted(os.listdir(html_dir))[:10]:
            if not html_file.endswith('.html'):
                continue
            path = os.path.join(html_dir, html_file)
            with open(path, 'r', encoding='utf-8') as f:
                content = f.read()
            
            # Count image dimensions
            img_items = re.findall(r'<img[^>]*width="(\d+)"[^>]*height="(\d+)"', content)
            zero_imgs = sum(1 for w, h in img_items if w == "0" or h == "0")
            total = len(img_items)
            placeholder = content.count('Image error')
            
            status = "OK" if zero_imgs == 0 else f"WARN: {zero_imgs} zero-dim"
            print(f"  {html_file[:60]:60s} imgs={total:3d} err={placeholder} {status}")
    
    # Summary
    print("\n" + "#"*60)
    print("  SUMMARY")
    print("#"*60)
    if errors:
        print("\n  ERRORS:")
        for e in errors:
            print(f"    - {e}")
        print(f"\n  Result: {len(errors)} issue(s) found")
        sys.exit(1)
    else:
        print("\n  ALL CHECKS PASSED!")
        sys.exit(0)

if __name__ == '__main__':
    main()
