import csv, zlib

unique_names = set()
crc_hashes = set()
with open('data/c/records.csv', 'r', encoding='utf-8', errors='ignore') as f:
    reader = csv.reader(f)
    next(reader, None) # skip header
    for row in reader:
        if len(row) >= 5:
            name = row[1]
            unique_names.add(name)
            # 64-bit composite: (len << 32) | crc32
            h = (len(name.encode('utf-8')) << 32) | zlib.crc32(name.encode('utf-8'))
            crc_hashes.add(h)

print(f"Unique strings: {len(unique_names)}")
print(f"Unique CRC64 hashes: {len(crc_hashes)}")
print(f"Collisions: {len(unique_names) - len(crc_hashes)}")
