import zlib

with open('data/a/requests.log', 'rb') as f:
    lines = f.readlines()

endpoints = set()
for line in lines:
    parts = line.split(b' ')
    if len(parts) >= 3:
        endpoints.add(parts[2])

print(f"Total unique endpoints: {len(endpoints)}")

# Test CRC32 collision
crcs = {}
collisions = 0
for ep in endpoints:
    c = zlib.crc32(ep)
    if c in crcs and crcs[c] != ep:
        collisions += 1
        print(f"Collision between {ep} and {crcs[c]}")
    crcs[c] = ep

print(f"Total CRC32 collisions among all {len(endpoints)} endpoints: {collisions}")
assert collisions == 0
