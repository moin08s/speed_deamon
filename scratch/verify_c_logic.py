import hashlib
import csv

with open('data/c/aggregates.txt', 'rb') as f:
    expected_hash = hashlib.sha256(f.read()).hexdigest()
print('Expected hash:', expected_hash)

# Read regions
region_to_country = {}
raw_countries = []
country_id_map = {}

with open('data/c/regions.csv', 'r', encoding='utf-8') as f:
    r = csv.reader(f)
    next(r)
    for row in r:
        if not row: continue
        rid = int(row[0].lstrip('\ufeff'))
        cname = row[1]
        if cname not in country_id_map:
            country_id_map[cname] = len(raw_countries)
            raw_countries.append(cname)
        region_to_country[rid] = country_id_map[cname]

unmatched_raw = len(raw_countries)
raw_countries.append('_UNMATCHED')
country_id_map['_UNMATCHED'] = unmatched_raw

sorted_countries = sorted(raw_countries)
raw_to_canonical = {raw_id: sorted_countries.index(cname) for raw_id, cname in enumerate(raw_countries)}

# Aggregation structures
class Stats:
    def __init__(self):
        self.count = 0
        self.sum_rec_id = 0
        self.sum_cents = 0
        self.min_cents = None
        self.max_cents = None
        self.sum_name_len = 0
        self.sum_note_len = 0
        self.names = set()

stats_by_country = [Stats() for _ in range(len(sorted_countries))]

with open('data/c/records.csv', 'r', encoding='utf-8') as f:
    r = csv.reader(f)
    next(r)
    for row in r:
        if not row: continue
        rec_id = int(row[0].lstrip('\ufeff'))
        name = row[1]
        note = row[2]
        reg_id = int(row[3])
        amount_str = row[4]
        
        # parse cents
        neg = amount_str.startswith('-')
        if neg: amount_str = amount_str[1:]
        parts = amount_str.split('.')
        whole = int(parts[0])
        frac = int(parts[1]) if len(parts) > 1 else 0
        cents = whole * 100 + frac
        if neg: cents = -cents
        
        raw_cid = region_to_country.get(reg_id, unmatched_raw)
        canon_cid = raw_to_canonical[raw_cid]
        
        st = stats_by_country[canon_cid]
        st.count += 1
        st.sum_rec_id += rec_id
        st.sum_cents += cents
        if st.min_cents is None or cents < st.min_cents: st.min_cents = cents
        if st.max_cents is None or cents > st.max_cents: st.max_cents = cents
        st.sum_name_len += len(name)
        st.sum_note_len += len(note)
        st.names.add(name)

lines = []
for cid, cname in enumerate(sorted_countries):
    st = stats_by_country[cid]
    if st.count == 0: continue
    
    def fmt_cents(c):
        neg = c < 0
        if neg: c = -c
        w = c // 100
        rem = c % 100
        prefix = '-' if neg else ''
        return f"{prefix}{w}.{rem:02d}"
        
    line = f"{cname} {st.count} {st.sum_rec_id} {fmt_cents(st.sum_cents)} {fmt_cents(st.min_cents)} {fmt_cents(st.max_cents)} {len(st.names)} {st.sum_name_len} {st.sum_note_len}\n"
    lines.append(line)

gen_bytes = "".join(lines).encode('utf-8')
gen_hash = hashlib.sha256(gen_bytes).hexdigest()
print('Generated hash:', gen_hash)
assert gen_hash == expected_hash
print('TRACK C PERFECT MATCH!')
