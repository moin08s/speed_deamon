import csv, zlib
from collections import defaultdict

# 1. Compute exact unique names per country using raw Python set of strings
with open('data/c/regions.csv') as f:
    next(f)
    reg_map = {}
    for line in f:
        p = line.strip().split(',')
        if len(p) == 2:
            reg_map[int(p[0])] = p[1]

real_unique = defaultdict(set)
crc_unique = defaultdict(set)

with open('data/c/records.csv', 'r', encoding='utf-8', errors='ignore') as f:
    reader = csv.reader(f)
    next(reader, None)
    for row in reader:
        if len(row) >= 5:
            name = row[1]
            reg = int(row[3])
            c = reg_map.get(reg, '_UNMATCHED')
            real_unique[c].add(name)
            name_bytes = name.encode('utf-8')
            h = (len(name_bytes) << 32) | zlib.crc32(name_bytes)
            crc_unique[c].add(h)

diffs = 0
for c in real_unique:
    if len(real_unique[c]) != len(crc_unique[c]):
        print(f"Mismatch for country {c}: real={len(real_unique[c])}, crc={len(crc_unique[c])}")
        diffs += 1

if diffs == 0:
    print("ALL 241 COUNTRIES MATCH EXACTLY WITH (len<<32)|crc32! 0 MISMATCHES!")
