with open('data/a/requests.log', 'rb') as f:
    lines = f.readlines()

eps = set()
byte_count = 0
found_line = 0
for i, line in enumerate(lines):
    parts = line.split(b' ')
    if len(parts) >= 3:
        eps.add(parts[2])
    byte_count += len(line)
    if len(eps) == 1842:
        found_line = i
        break

print(f"Total unique endpoints: {len(eps)}")
print(f"Found all 1842 endpoints at line {found_line} out of {len(lines)} ({byte_count / (1024*1024):.2f} MB)")
