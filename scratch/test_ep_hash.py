with open('data/a/requests.log', 'rb') as f:
    eps = set()
    for l in f:
        eps.add(l.split(b' ')[2])

print('Total unique endpoints:', len(eps))

hashes = {}
collisions = 0
for ep in eps:
    L = len(ep)
    # first 8 bytes and last 8 bytes (padded if L < 8)
    first8 = ep[:min(8, L)].ljust(8, b'\0')
    last8 = ep[max(0, L-8):].rjust(8, b'\0')
    key = (L, first8, last8)
    if key in hashes:
        collisions += 1
        print(f"Collision between: {ep} and {hashes[key]}")
    else:
        hashes[key] = ep

print(f"Collisions among {len(eps)} endpoints: {collisions}")
