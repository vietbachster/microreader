"""Download a curated, diverse set of Project Gutenberg EPUBs for testing.

Covers:
- Multiple languages (English, German, French, Spanish, Italian, Portuguese, Chinese, Japanese)
- Different eras (ancient works through 20th century)
- Different genres (fiction, non-fiction, poetry, drama, philosophy, science)
- Different sizes (short stories to massive novels)
- Different formatting (simple text, footnotes, illustrations)
"""

import os
import sys
import time
import urllib.request
import ssl

# Gutenberg EPUB3 download URL pattern
# Format: https://www.gutenberg.org/ebooks/{id}.epub3.images
# or:     https://www.gutenberg.org/ebooks/{id}.epub.images
BASE_URL = "https://www.gutenberg.org/cache/epub/{id}/pg{id}-images.epub"

# Curated list: (id, description)
BOOKS = [
    # === ENGLISH - Classic Fiction ===
    (1342, "Pride and Prejudice - Jane Austen"),        # already have
    (84,   "Frankenstein - Mary Shelley"),              # already have
    (1661, "Sherlock Holmes - Arthur Conan Doyle"),     # already have
    (11,   "Alice in Wonderland - Lewis Carroll"),      # already have
    (2701, "Moby Dick - Herman Melville"),              # already have
    (1952, "The Yellow Wallpaper - Charlotte Gilman"),  # already have (short)
    
    # English - More classics
    (100,  "Shakespeare Complete Works"),               # HUGE
    (4300, "Ulysses - James Joyce"),                    # Complex formatting
    (1400, "Great Expectations - Dickens"),
    (25344,"The Scarlet Pimpernel - Baroness Orczy"),
    (98,   "A Tale of Two Cities - Dickens"),           # already have
    (161,  "Sense and Sensibility - Jane Austen"),
    (1260, "Jane Eyre - Charlotte Bronte"),             # already have
    (768,  "Wuthering Heights - Emily Bronte"),
    (43,   "The Strange Case of Dr Jekyll and Mr Hyde"),
    (36,   "The War of the Worlds - HG Wells"),
    (35,   "The Time Machine - HG Wells"),
    (120,  "Treasure Island - Stevenson"),
    (3207, "Leviathan - Thomas Hobbes"),                # Philosophy
    (16,   "Peter Pan - JM Barrie"),
    (244,  "A Study in Scarlet - Conan Doyle"),
    (514,  "Little Women - Louisa May Alcott"),
    (2554, "Crime and Punishment - Dostoevsky (English)"),
    (1232, "The Prince - Machiavelli (English)"),       # already have
    
    # English - Science/Non-fiction
    (4363, "On the Origin of Species - Darwin"),
    (28054,"The Brothers Karamazov - Dostoevsky (English)"),
    (5827, "The Problems of Philosophy - Bertrand Russell"),
    (3600, "Beyond Good and Evil - Nietzsche (English)"),
    (1497, "The Republic - Plato (English)"),
    (996,  "Don Quixote - Cervantes (English)"),
    
    # English - Poetry
    (1065, "Leaves of Grass - Walt Whitman"),
    (8800, "The Divine Comedy - Dante (English)"),
    
    # English - Drama
    (1524, "Hamlet - Shakespeare"),
    
    # English - 20th century (public domain varies by country)
    (164,  "Twenty Thousand Leagues - Jules Verne (English)"),
    (103,  "Around the World in 80 Days - Verne (English)"),
    
    # === GERMAN ===
    (2229, "Faust - Goethe"),
    (5323, "Also sprach Zarathustra - Nietzsche"),
    (7205, "Die Verwandlung - Kafka"),
    (22367,"Der Prozess - Kafka"),
    (29631,"Siddhartha - Hermann Hesse"),
    (6498, "Die Leiden des jungen Werther - Goethe"),
    (30601,"Der Steppenwolf - Hesse"),
    (9846, "Grimms Märchen"),
    (2407, "Wilhelm Tell - Schiller"),
    
    # === FRENCH ===
    (13951,"Les Misérables - Victor Hugo"),
    (17489,"Le Petit Prince - Saint-Exupéry"),
    (4650, "Les Trois Mousquetaires - Dumas"),
    (13704,"Le Comte de Monte-Cristo - Dumas"),
    (5185, "Candide - Voltaire (French)"),
    (14287,"Germinal - Émile Zola"),
    (17396,"Madame Bovary - Flaubert"),
    
    # === SPANISH ===
    (2000, "Don Quijote - Cervantes (Spanish)"),
    (15532,"Cien Años de Soledad sample"),  # may not exist
    
    # === ITALIAN ===
    (1012, "La Divina Commedia - Dante"),
    (3601, "I Promessi Sposi - Manzoni"),
    
    # === PORTUGUESE ===
    (17525,"Os Lusíadas - Camões"),
    
    # === CHINESE ===
    (24264,"Art of War - Sun Tzu (Chinese)"),  # might be English
    (25328,"Chinese text sample"),
    
    # === JAPANESE ===
    (1982, "Kokoro - Natsume Soseki"),  # might be English translation
    
    # === LATIN ===
    (7700, "De Bello Gallico - Julius Caesar"),
    
    # === SPECIAL FORMAT TESTS ===
    (76,   "Adventures of Huckleberry Finn - Mark Twain"),  # already have, with illustrations
    (74,   "Adventures of Tom Sawyer - Mark Twain"),        # already have
    (46,   "A Christmas Carol - Dickens"),                  # already have, with images
    (219,  "Heart of Darkness - Joseph Conrad"),            # already have
    (345,  "Dracula - Bram Stoker"),                        # already have
    (1080, "A Modest Proposal - Jonathan Swift"),           # already have (very short)
]

def download_book(book_id, desc, out_dir, delay=2.0):
    filename = f"pg{book_id}-images.epub"
    filepath = os.path.join(out_dir, filename)
    
    if os.path.exists(filepath):
        size = os.path.getsize(filepath)
        if size > 1000:  # not an error page
            return "exists", size
    
    url = BASE_URL.format(id=book_id)
    
    try:
        ctx = ssl.create_default_context()
        req = urllib.request.Request(url, headers={
            'User-Agent': 'MicroreaderTestBot/1.0 (epub-test-suite; contact: test@example.com)'
        })
        with urllib.request.urlopen(req, timeout=30, context=ctx) as resp:
            data = resp.read()
            if len(data) < 500:
                return "too_small", len(data)
            with open(filepath, 'wb') as f:
                f.write(data)
            time.sleep(delay)
            return "ok", len(data)
    except urllib.error.HTTPError as e:
        return f"http_{e.code}", 0
    except Exception as e:
        return f"error: {e}", 0


def main():
    out_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)), 
                           "..", "books", "gutenberg")
    os.makedirs(out_dir, exist_ok=True)
    
    # Deduplicate by ID
    seen = set()
    unique_books = []
    for bid, desc in BOOKS:
        if bid not in seen:
            seen.add(bid)
            unique_books.append((bid, desc))
    
    total = len(unique_books)
    ok = 0
    skipped = 0
    failed = 0
    
    print(f"Downloading {total} diverse Gutenberg EPUBs to {out_dir}")
    print(f"Respecting 2-second delay between requests\n")
    
    for i, (bid, desc) in enumerate(unique_books, 1):
        status, size = download_book(bid, desc, out_dir)
        if status == "exists":
            print(f"  [{i}/{total}] SKIP pg{bid} ({desc}) — already exists ({size} bytes)")
            skipped += 1
        elif status == "ok":
            print(f"  [{i}/{total}] OK   pg{bid} ({desc}) — {size} bytes")
            ok += 1
        else:
            print(f"  [{i}/{total}] FAIL pg{bid} ({desc}) — {status}")
            failed += 1
    
    print(f"\nDone: {ok} downloaded, {skipped} already existed, {failed} failed")
    print(f"Total EPUBs in directory: {len([f for f in os.listdir(out_dir) if f.endswith('.epub')])}")


if __name__ == "__main__":
    main()
