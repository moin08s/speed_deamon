with open('data/c/records.csv', 'r', encoding='utf-8') as f:
    names = set()
    for line in f:
        parts = line.strip().split(',')
        if len(parts) >= 3:
            names.add(parts[2].strip('"'))
print(f'Total distinct names in dataset: {len(names)}')

# Test 48-bit FNV hash collisions
h48_set = set()
collision = False
for name in names:
    h = 0xcbf29ce484222325
    for c in name.encode('utf-8'):
        h = ((h ^ c) * 0x100000001b3) & 0xFFFFFFFFFFFFFFFF
    h48 = h & 0xFFFFFFFFFFFF
    if h48 in h48_set:
        print('COLLISION in 48-bit hash for:', name)
        collision = True
        break
    h48_set.add(h48)
if not collision:
    print('ZERO collisions in 48-bit hash among all distinct names!')
