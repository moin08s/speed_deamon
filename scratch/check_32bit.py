import csv
from collections import defaultdict

country_names = defaultdict(set)
with open('data/c/records.csv', 'r', encoding='utf-8') as f:
    r = csv.reader(f)
    next(r)
    for row in r:
        if not row: continue
        name = row[1]
        reg_id = int(row[3])
        country_names[reg_id].add(name)

# Check per-country 32-bit hash collisions
total_countries = len(country_names)
collisions = 0
for reg_id, names in country_names.items():
    h_set = set()
    for n in names:
        h = 0x811c9dc5
        for c in n.encode('utf-8'):
            h = ((h ^ c) * 0x01000193) & 0xFFFFFFFF
        if h in h_set:
            collisions += 1
            break
        h_set.add(h)

print(f'Total region groups: {total_countries}, Groups with 32-bit collision: {collisions}')
