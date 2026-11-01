import hashlib
from collections import defaultdict

# 1. Parse queries to get term_table
terms_list = []
term_to_id = {}
queries = []
with open('data/b/queries.txt', 'r', encoding='utf-8') as f:
    for line in f:
        line = line.strip()
        if not line: continue
        parts = line.split()
        qtype = parts[0]
        q_terms = []
        for p in parts[1:]:
            if p not in ['AND', 'OR', 'NOT']:
                p_low = p.lower()
                if p_low not in term_to_id:
                    term_to_id[p_low] = len(terms_list)
                    terms_list.append(p_low)
                q_terms.append(term_to_id[p_low])
        queries.append((qtype, parts[1:], q_terms))

num_terms = len(terms_list)
print(f"Num terms: {num_terms}")

# 2. Collect postings in a single pass
postings = defaultdict(list)
doc_texts = {}

with open('data/b/docs.txt', 'r', encoding='utf-8') as f:
    for line in f:
        line = line.strip('\r\n')
        if not line: continue
        parts = line.split('\t', 1)
        if len(parts) < 2: continue
        doc_id = int(parts[0])
        text = parts[1]
        doc_texts[doc_id] = text

        # Tokenize words
        words = []
        cur = []
        for c in text:
            if c.isalnum():
                cur.append(c.lower())
            else:
                if cur:
                    words.append("".join(cur))
                    cur = []
        if cur:
            words.append("".join(cur))

        seen = set()
        for w in words:
            if w in term_to_id and w not in seen:
                seen.add(w)
                tid = term_to_id[w]
                postings[tid].append(doc_id)

print(f"Terms with postings: {len(postings)}")
for tid in postings:
    postings[tid].sort()

# Check sample query
print("Testing single-pass inverted index logic...")
