import csv, zlib, hashlib
from collections import defaultdict

# 1. Load regions
reg_map = {}
with open('data/c/regions.csv') as f:
    next(f)
    for line in f:
        p = line.strip().split(',')
        if len(p) == 2:
            reg_map[int(p[0])] = p[1]

# Countries
countries = sorted(list(set(reg_map.values()) | {'_UNMATCHED'}))

# Stats: count, sum_rec, sum_cents, min_cents, max_cents, unique_hashes, sum_name, sum_note
stats = {c: [0, 0, 0, None, None, set(), 0, 0] for c in countries}

with open('data/c/records.csv', 'r', encoding='utf-8', errors='ignore') as f:
    reader = csv.reader(f)
    next(reader, None)
    for row in reader:
        if len(row) >= 5:
            rec_id = int(row[0].lstrip('\ufeff'))
            name = row[1]
            note = row[2]
            reg_id = int(row[3])
            
            # cents
            cents_str = row[4].strip()
            neg = cents_str.startswith('-')
            if neg: cents_str = cents_str[1:]
            if '.' in cents_str:
                w, rem = cents_str.split('.')
                cents = int(w) * 100 + int(rem[:2].ljust(2, '0'))
            else:
                cents = int(cents_str) * 100
            if neg: cents = -cents

            c = reg_map.get(reg_id, '_UNMATCHED')
            st = stats[c]
            st[0] += 1
            st[1] += rec_id
            st[2] += cents
            if st[3] is None or cents < st[3]: st[3] = cents
            if st[4] is None or cents > st[4]: st[4] = cents
            
            # (len << 32) | crc32
            name_bytes = name.encode('utf-8')
            h = (len(name_bytes) << 32) | zlib.crc32(name_bytes)
            st[5].add(h)
            st[6] += len(name_bytes)
            st[7] += len(note.encode('utf-8'))

out_lines = []
for c in countries:
    st = stats[c]
    if st[0] == 0: continue
    
    def fmt_cents(cts):
        sgn = '-' if cts < 0 else ''
        cts = abs(cts)
        return f"{sgn}{cts//100}.{cts%100:02d}"

    line = f"{c} {st[0]} {st[1]} {fmt_cents(st[2])} {fmt_cents(st[3])} {fmt_cents(st[4])} {len(st[5])} {st[6]} {st[7]}\n"
    out_lines.append(line)

out_text = "".join(out_lines).encode('utf-8')
calc_sha = hashlib.sha256(out_text).hexdigest()

with open('data/c/aggregates.txt', 'rb') as f:
    expected_sha = hashlib.sha256(f.read()).hexdigest()

print("Calculated sha256:", calc_sha)
print("Expected   sha256:", expected_sha)
print("Match:", calc_sha == expected_sha)
