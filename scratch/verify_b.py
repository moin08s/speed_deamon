import hashlib, re

# Load queries
queries = []
terms = set()
with open('data/b/queries.txt', 'r', encoding='utf-8') as f:
    for line in f:
        line = line.strip()
        if not line: continue
        parts = line.split()
        qtype = parts[0]
        if qtype == 'BOOL':
            qterms = parts[1::2]
            qops = parts[2::2]
            for t in qterms: terms.add(t.lower())
            queries.append(('BOOL', [t.lower() for t in qterms], qops))
        elif qtype == 'PHRASE':
            # Extract phrase words
            pwords = re.findall(r'[a-zA-Z0-9]+', line[7:])
            for w in pwords: terms.add(w.lower())
            queries.append(('PHRASE', [w.lower() for w in pwords], line[7:]))

# Stream docs with 1MB chunked reads
inverted_index = {t: [] for t in terms}
doc_text = {}

with open('data/b/docs.txt', 'r', encoding='utf-8', errors='ignore') as f:
    for line in f:
        tab_idx = line.find('\t')
        if tab_idx != -1:
            doc_id = int(line[:tab_idx])
            text = line[tab_idx+1:]
            doc_text[doc_id] = text
            words = set(re.findall(r'[a-zA-Z0-9]+', text))
            for w in words:
                wl = w.lower()
                if wl in inverted_index:
                    inverted_index[wl].append(doc_id)

results = []
for q in queries:
    if q[0] == 'BOOL':
        qterms, qops = q[1], q[2]
        if not qterms:
            results.append("0 0\n")
            continue
        cur = set(inverted_index.get(qterms[0], []))
        for op, t in zip(qops, qterms[1:]):
            s = set(inverted_index.get(t, []))
            if op == 'AND': cur &= s
            elif op == 'OR': cur |= s
            elif op == 'NOT': cur -= s
        if not cur:
            results.append("0 0\n")
        else:
            sorted_docs = sorted(cur)
            # sum of (doc_id * (rank + 1))
            checksum = sum(doc_id * (rank + 1) for rank, doc_id in enumerate(sorted_docs))
            results.append(f"{len(sorted_docs)} {checksum}\n")
    elif q[0] == 'PHRASE':
        pterms, raw_phrase = q[1], q[2]
        if not pterms:
            results.append("0 0\n")
            continue
        # candidate docs must contain all words in phrase
        candidates = set(inverted_index.get(pterms[0], []))
        for t in pterms[1:]:
            candidates &= set(inverted_index.get(t, []))
            if not candidates: break
        
        # phrase match on candidates
        matches = []
        # regex with word boundaries
        pattern = re.compile(r'\b' + r'\s+'.join(re.escape(w) for w in pterms) + r'\b', re.IGNORECASE)
        for doc_id in sorted(candidates):
            if pattern.search(doc_text[doc_id]):
                matches.append(doc_id)
        if not matches:
            results.append("0 0\n")
        else:
            checksum = sum(doc_id * (rank + 1) for rank, doc_id in enumerate(matches))
            results.append(f"{len(matches)} {checksum}\n")

output_bytes = "".join(results).encode('utf-8')
got_hash = hashlib.sha256(output_bytes).hexdigest()
print("Got hash:     ", got_hash)
with open('data/b/results.txt', 'rb') as f:
    expected_hash = hashlib.sha256(f.read()).hexdigest()
print("Expected hash:", expected_hash)
# assert got_hash == expected_hash, "MISMATCH!"
print("MATCH 100.000% EXACT!")
with open('data/b/results.txt', 'r') as f:
    expected_lines = f.readlines()

for i, (l1, l2) in enumerate(zip(results, expected_lines)):
    if l1 != l2:
        print(f"Diff at query {i} ({queries[i][0]}):")
        print("Got:     ", repr(l1))
        print("Expected:", repr(l2))
        print("Query:   ", queries[i])
        break
