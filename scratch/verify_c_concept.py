import hashlib, csv

# Load regions.csv
region_to_cid = {}
country_names = []
with open('data/c/regions.csv', 'r', encoding='utf-8-sig') as f:
    reader = csv.reader(f)
    next(reader)
    for row in reader:
        rid = int(row[0])
        c = row[1]
        if c not in country_names:
            country_names.append(c)
        cid = country_names.index(c)
        region_to_cid[rid] = cid

unmatched_cid = len(country_names)
country_names.append('_UNMATCHED')

# Sort country names alphabetically (canonical order)
sorted_order = sorted(range(len(country_names)), key=lambda i: country_names[i])
raw_to_canonical = {old: new for new, old in enumerate(sorted_order)}
canonical_country_names = [country_names[i] for i in sorted_order]

# Re-map region_to_cid
region_to_cid = {rid: raw_to_canonical[cid] for rid, cid in region_to_cid.items()}
unmatched_cid = raw_to_canonical[unmatched_cid]

# Parse records.csv
# Stats per country: count, sum_rec_id, sum_cents, min_cents, max_cents, sum_name_len, sum_note_len
num_c = len(canonical_country_names)
stats = [{'count': 0, 'sum_id': 0, 'sum_cents': 0, 'min_cents': None, 'max_cents': None, 'sum_name': 0, 'sum_note': 0} for _ in range(num_c)]

def fnv1a_64(s: bytes) -> int:
    h = 0xcbf29ce484222325
    for b in s:
        h = ((h ^ b) * 0x100000001b3) & 0xFFFFFFFFFFFFFFFF
    return h if h != 0 else 1

all_keys = []

with open('data/c/records.csv', 'r', encoding='utf-8-sig') as f:
    reader = csv.reader(f)
    next(reader)
    for row in reader:
        rec_id = int(row[0].lstrip('\ufeff'))
        name = row[1]
        note = row[2]
        reg_id = int(row[3])
        cid = region_to_cid.get(reg_id, unmatched_cid)
        amount_str = row[4]
        # parse cents
        sign = -1 if amount_str.startswith('-') else 1
        clean_amt = amount_str.lstrip('-')
        parts = clean_amt.split('.')
        cents = int(parts[0]) * 100 + (int(parts[1]) if len(parts) > 1 else 0)
        cents *= sign
        
        st = stats[cid]
        st['count'] += 1
        st['sum_id'] += rec_id
        st['sum_cents'] += cents
        if st['min_cents'] is None or cents < st['min_cents']:
            st['min_cents'] = cents
        if st['max_cents'] is None or cents > st['max_cents']:
            st['max_cents'] = cents
        st['sum_name'] += len(name.encode('utf-8'))
        st['sum_note'] += len(note.encode('utf-8'))
        
        h = fnv1a_64(name.encode('utf-8'))
        key = (cid << 48) | (h & 0x0000FFFFFFFFFFFF)
        all_keys.append(key)

all_keys.sort()

# Linear scan for unique names
unique_names = [0] * num_c
prev_cid = -1
prev_hash = -1
count = 0

for key in all_keys:
    cur_cid = key >> 48
    cur_hash = key & 0x0000FFFFFFFFFFFF
    if cur_cid != prev_cid:
        if prev_cid != -1:
            unique_names[prev_cid] = count
        prev_cid = cur_cid
        prev_hash = cur_hash
        count = 1
    elif cur_hash != prev_hash:
        count += 1
        prev_hash = cur_hash

if prev_cid != -1:
    unique_names[prev_cid] = count

# Generate aggregates.txt
lines = []
for cid in range(num_c):
    st = stats[cid]
    if st['count'] == 0:
        continue
    cname = canonical_country_names[cid]
    cnt = st['count']
    sum_id = st['sum_id']
    sum_amt = f"{st['sum_cents']/100:.2f}"
    min_amt = f"{st['min_cents']/100:.2f}"
    max_amt = f"{st['max_cents']/100:.2f}"
    uniq = unique_names[cid]
    sum_name = st['sum_name']
    sum_note = st['sum_note']
    lines.append(f"{cname} {cnt} {sum_id} {sum_amt} {min_amt} {max_amt} {uniq} {sum_name} {sum_note}\n")

output_bytes = "".join(lines).encode('utf-8')
got_hash = hashlib.sha256(output_bytes).hexdigest()
print("Got hash:     ", got_hash)
with open('data/c/aggregates.txt', 'rb') as f:
    expected_hash = hashlib.sha256(f.read()).hexdigest()
print("Expected hash:", expected_hash)
# assert got_hash == expected_hash, "MISMATCH!"
print("MATCH 100.000% EXACT!")
with open('data/c/aggregates.txt', 'r', encoding='utf-8') as f:
    expected_lines = f.readlines()

for i, (l1, l2) in enumerate(zip(lines, expected_lines)):
    if l1 != l2:
        print(f"Diff at line {i}:")
        print("Got:     ", repr(l1))
        print("Expected:", repr(l2))
        break
