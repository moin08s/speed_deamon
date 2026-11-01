import hashlib

with open('data/c/records.csv', 'rb') as f:
    mf_data = f.read()

with open('data/c/regions.csv', 'rb') as f:
    reg_data = f.read()

# Parse regions
reg_lines = reg_data.split(b'\n')
region_to_country = {}
country_set = set()
for line in reg_lines[1:]:
    if not line: continue
    parts = line.split(b',')
    if len(parts) == 2:
        rid = int(parts[0])
        c = parts[1].strip()
        region_to_country[rid] = c
        country_set.add(c)

all_countries = sorted(list(country_set)) + [b'_UNMATCHED']
c_to_idx = {c: i for i, c in enumerate(all_countries)}

p = 0
limit = len(mf_data)

# skip BOM and header
if limit - p >= 3 and mf_data[p:p+3] == b'\xef\xbb\xbf': p += 3
while p < limit and mf_data[p] != 10: p += 1
if p < limit and mf_data[p] == 10: p += 1

class CountryStats:
    def __init__(self):
        self.count = 0
        self.sum_rec_id = 0
        self.sum_cents = 0
        self.min_cents = None
        self.max_cents = None
        self.sum_name_len = 0
        self.sum_note_len = 0
        self.name_hashes = set()

stats = [CountryStats() for _ in range(len(all_countries))]

while p < limit:
    while p < limit and mf_data[p] in (10, 13): p += 1
    if p >= limit: break
    
    # id with BOM
    if limit - p >= 3 and mf_data[p:p+3] == b'\xef\xbb\xbf': p += 3
    rec_id = 0
    while p < limit and 48 <= mf_data[p] <= 57:
        rec_id = rec_id * 10 + (mf_data[p] - 48)
        p += 1
    if p < limit and mf_data[p] == 44: p += 1
    
    # name
    out_hash = 0xcbf29ce484222325
    name_len = 0
    if p < limit and mf_data[p] == 34:
        p += 1
        while p < limit:
            q = mf_data.find(b'"', p)
            if q == -1:
                name_len += (limit - p)
                for k in range(p, limit):
                    out_hash = ((out_hash ^ mf_data[k]) * 0x100000001b3) & 0xffffffffffffffff
                p = limit
                break
            name_len += (q - p)
            for k in range(p, q):
                out_hash = ((out_hash ^ mf_data[k]) * 0x100000001b3) & 0xffffffffffffffff
            if q + 1 < limit and mf_data[q+1] == 34:
                name_len += 1
                out_hash = ((out_hash ^ 34) * 0x100000001b3) & 0xffffffffffffffff
                p = q + 2
            else:
                p = q + 1
                break
        if p < limit and mf_data[p] == 44: p += 1
    else:
        s = p
        while p < limit and mf_data[p] not in (44, 10, 13):
            out_hash = ((out_hash ^ mf_data[p]) * 0x100000001b3) & 0xffffffffffffffff
            p += 1
        name_len = p - s
        if p < limit and mf_data[p] == 44: p += 1
        
    # note
    note_len = 0
    if p < limit and mf_data[p] == 34:
        p += 1
        while p < limit:
            q = mf_data.find(b'"', p)
            if q == -1:
                note_len += (limit - p)
                p = limit
                break
            note_len += (q - p)
            if q + 1 < limit and mf_data[q+1] == 34:
                note_len += 1
                p = q + 2
            else:
                p = q + 1
                break
        if p < limit and mf_data[p] == 44: p += 1
    else:
        s = p
        while p < limit and mf_data[p] not in (44, 10, 13): p += 1
        note_len = p - s
        if p < limit and mf_data[p] == 44: p += 1
        
    # region_id
    reg_id = 0
    while p < limit and 48 <= mf_data[p] <= 57:
        reg_id = reg_id * 10 + (mf_data[p] - 48)
        p += 1
    if p < limit and mf_data[p] == 44: p += 1
    
    # cents
    neg = (p < limit and mf_data[p] == 45)
    if neg: p += 1
    cents = 0
    while p < limit and 48 <= mf_data[p] <= 57:
        cents = cents * 10 + (mf_data[p] - 48)
        p += 1
    if p < limit and mf_data[p] == 46: p += 1
    if p + 1 < limit:
        cents = cents * 100 + (mf_data[p] - 48) * 10 + (mf_data[p+1] - 48)
        p += 2
    while p < limit and mf_data[p] in (10, 13): p += 1
    if neg: cents = -cents
    
    c_name = region_to_country.get(reg_id, b'_UNMATCHED')
    cid = c_to_idx[c_name]
    cs = stats[cid]
    cs.count += 1
    cs.sum_rec_id += rec_id
    cs.sum_cents += cents
    if cs.min_cents is None or cents < cs.min_cents: cs.min_cents = cents
    if cs.max_cents is None or cents > cs.max_cents: cs.max_cents = cents
    cs.sum_name_len += name_len
    cs.sum_note_len += note_len
    cs.name_hashes.add(out_hash)

# Format output
out_lines = []
for cid, cname in enumerate(all_countries):
    cs = stats[cid]
    if cs.count == 0: continue
    
    def fmt_cents(val):
        prefix = '-' if val < 0 else ''
        val = abs(val)
        return f"{prefix}{val // 100}.{val % 100:02d}"
        
    line = f"{cname.decode()} {cs.count} {cs.sum_rec_id} {fmt_cents(cs.sum_cents)} {fmt_cents(cs.min_cents)} {fmt_cents(cs.max_cents)} {len(cs.name_hashes)} {cs.sum_name_len} {cs.sum_note_len}\n"
    out_lines.append(line.encode())

generated_output = b''.join(out_lines)
with open('data/c/aggregates.txt', 'rb') as f:
    ref_output = f.read()

gen_hash = hashlib.sha256(generated_output).hexdigest()
ref_hash = hashlib.sha256(ref_output).hexdigest()

print("Generated SHA-256:", gen_hash)
print("Reference SHA-256:", ref_hash)
print("MATCH:", gen_hash == ref_hash)
assert gen_hash == ref_hash
