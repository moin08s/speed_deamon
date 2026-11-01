import re, hashlib
from collections import defaultdict

# 1. Parse queries
with open('data/b/queries.txt') as f:
    q_lines = f.readlines()

term_to_id = {}
queries = []
for line in q_lines:
    line = line.strip()
    if not line: continue
    if line.startswith('BOOL '):
        parts = line[5:].split()
        q = ('BOOL', [])
        is_op = False
        for p in parts:
            if not is_op:
                w = p.lower()
                if w not in term_to_id:
                    term_to_id[w] = len(term_to_id)
                q[1].append(('TERM', term_to_id[w]))
                is_op = True
            else:
                q[1].append(('OP', p))
                is_op = False
        queries.append(q)
    elif line.startswith('PHRASE '):
        words = re.findall(r'[a-z0-9]+', line[7:].lower())
        q = ('PHRASE', [], words)
        for w in words:
            if w not in term_to_id:
                term_to_id[w] = len(term_to_id)
            q[1].append(term_to_id[w])
        queries.append(q)

num_terms = len(term_to_id)
print(f"Num terms: {num_terms}")

# Pass 1: Count term frequencies
term_counts = [0] * num_terms
docs = []
with open('data/b/docs.txt', encoding='utf-8', errors='ignore') as f:
    for line in f:
        tab = line.find('\t')
        if tab != -1:
            did = int(line[:tab])
            text = line[tab+1:]
            words = set(re.findall(r'[a-z0-9]+', text.lower()))
            docs.append((did, text))
            for w in words:
                if w in term_to_id:
                    tid = term_to_id[w]
                    term_counts[tid] += 1

# Prefix sums
term_offsets = [0] * (num_terms + 1)
cur = 0
for i in range(num_terms):
    term_offsets[i] = cur
    cur += term_counts[i]
term_offsets[num_terms] = cur
total_postings = cur
print(f"Total postings: {total_postings}")

# Pass 2: Direct scatter into postings_data
postings_data = [0] * total_postings
cur_slots = list(term_offsets[:num_terms])

for did, text in docs:
    words = set(re.findall(r'[a-z0-9]+', text.lower()))
    for w in words:
        if w in term_to_id:
            tid = term_to_id[w]
            slot = cur_slots[tid]
            cur_slots[tid] += 1
            postings_data[slot] = did

print("Postings scattered successfully into flat array!")

# Verify query outputs match results.txt
out_lines = []
for q in queries:
    if q[0] == 'BOOL':
        terms = [item for item in q[1] if item[0] == 'TERM']
        ops = [item for item in q[1] if item[0] == 'OP']
        if not terms:
            out_lines.append("0 0\n")
            continue
        
        # Get first postings
        t0 = terms[0][1]
        st = term_offsets[t0]
        en = term_offsets[t0 + 1]
        res = set(postings_data[st:en])
        
        for idx, op in enumerate(ops):
            nxt_t = terms[idx + 1][1]
            st = term_offsets[nxt_t]
            en = term_offsets[nxt_t + 1]
            nxt_set = set(postings_data[st:en])
            if op[1] == 'AND':
                res.intersection_update(nxt_set)
            elif op[1] == 'OR':
                res.update(nxt_set)
            elif op[1] == 'NOT':
                res.difference_update(nxt_set)
        
        sorted_res = sorted(list(res))
        cnt = len(sorted_res)
        sm = sum(sorted_res)
        top10 = " ".join(str(x) for x in sorted_res[:10])
        line = f"{cnt} {sm}" + (f" {top10}" if cnt > 0 else "") + "\n"
        out_lines.append(line)
        
    elif q[0] == 'PHRASE':
        phrase_words = q[2]
        p_tids = q[1]
        if not p_tids:
            out_lines.append("0 0\n")
            continue
        
        # Candidate docs: intersection of all phrase terms
        t0 = p_tids[0]
        st = term_offsets[t0]
        en = term_offsets[t0 + 1]
        cands = set(postings_data[st:en])
        for tid in p_tids[1:]:
            st = term_offsets[tid]
            en = term_offsets[tid + 1]
            cands.intersection_update(set(postings_data[st:en]))
        
        res = []
        for did in sorted(list(cands)):
            txt = docs[did][1]
            doc_words = re.findall(r'[a-z0-9]+', txt.lower())
            # Match exact phrase sequence
            pw_len = len(phrase_words)
            matched = False
            for i in range(len(doc_words) - pw_len + 1):
                if doc_words[i:i+pw_len] == phrase_words:
                    matched = True
                    break
            if matched:
                res.append(did)
        
        cnt = len(res)
        sm = sum(res)
        top10 = " ".join(str(x) for x in res[:10])
        line = f"{cnt} {sm}" + (f" {top10}" if cnt > 0 else "") + "\n"
        out_lines.append(line)

calc_sha = hashlib.sha256("".join(out_lines).encode()).hexdigest()
with open('data/b/results.txt', 'rb') as f:
    expected_sha = hashlib.sha256(f.read()).hexdigest()

print("Calculated sha256:", calc_sha)
print("Expected   sha256:", expected_sha)
print("Match:", calc_sha == expected_sha)
