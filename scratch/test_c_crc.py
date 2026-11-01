import zlib

with open('data/c/records.csv', 'rb') as f:
    data = f.read()

# Collect names
names = set()
p = 0
limit = len(data)
while p < limit and data[p] != 10: p += 1
if p < limit: p += 1

while p < limit:
    while p < limit and data[p] in (10, 13): p += 1
    if p >= limit: break
    # id
    while p < limit and data[p] != 44: p += 1
    if p < limit: p += 1
    # name
    if p < limit and data[p] == 34:
        p += 1
        name_parts = []
        while p < limit:
            q = data.find(b'"', p)
            if q == -1: name_parts.append(data[p:]); p = limit; break
            name_parts.append(data[p:q])
            if q + 1 < limit and data[q+1] == 34:
                name_parts.append(b'"')
                p = q + 2
            else:
                p = q + 1
                break
        names.add(b''.join(name_parts))
    else:
        s = p
        while p < limit and data[p] not in (44, 10, 13): p += 1
        names.add(data[s:p])
    while p < limit and data[p] != 10: p += 1
    if p < limit: p += 1

print(f"Total unique names: {len(names)}")
# Check CRC64 / CRC32
c32 = set(zlib.crc32(n) for n in names)
print(f"CRC32 unique: {len(c32)} vs {len(names)} (collisions: {len(names) - len(c32)})")
c64 = set(((len(n) << 32) | zlib.crc32(n)) for n in names)
print(f"Length+CRC32 unique: {len(c64)} vs {len(names)} (collisions: {len(names) - len(c64)})")
