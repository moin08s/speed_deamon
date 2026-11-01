import time, hashlib

t0 = time.perf_counter()

GLOBAL_HOURS = [
    f"2026-10-30T{h:02d}:00:00Z" for h in range(18, 24)
] + [
    f"2026-10-31T{h:02d}:00:00Z" for h in range(0, 24)
] + [
    f"2026-11-01T{h:02d}:00:00Z" for h in range(0, 24)
] + [
    f"2026-11-02T{h:02d}:00:00Z" for h in range(0, 18)
]

def get_hour_idx(p):
    h = (p[11] - 48) * 10 + (p[12] - 48)
    if p[6] == 48:
        return (h - 18) if p[9] == 48 else (6 + h)
    else:
        return (30 + h) if p[9] == 49 else (54 + h)

with open('data/a/requests.log', 'rb') as f:
    data = f.read()

# Pass 1: Discover endpoints & count per (h, ep)
lines = data.split(b'\n')
if lines and not lines[-1]: lines.pop()

ep_map = {}
ep_list = []

# First pass: map endpoints
for line in lines:
    parts = line.split(b' ')
    ep = parts[2]
    if ep not in ep_map:
        ep_map[ep] = len(ep_list)
        ep_list.append(ep)

# Canonical sort
sorted_eids = sorted(range(len(ep_list)), key=lambda i: ep_list[i])
remap = [0] * len(ep_list)
for new_id, old_id in enumerate(sorted_eids):
    remap[old_id] = new_id

sorted_ep_names = [ep_list[i] for i in sorted_eids]
num_eps = len(sorted_ep_names)
print(f"Discovered {num_eps} endpoints in {time.perf_counter() - t0:.3f}s")

# Pass 1 counts
counts = [[0] * num_eps for _ in range(72)]
errors = [[0] * num_eps for _ in range(72)]

for line in lines:
    hid = get_hour_idx(line)
    parts = line.split(b' ')
    ep = parts[2]
    eid = remap[ep_map[ep]]
    counts[hid][eid] += 1
    if parts[3][0] == 53: # '5'
        errors[hid][eid] += 1

# Prefix sums
offsets = [[0] * num_eps for _ in range(72)]
total = 0
for h in range(72):
    for e in range(num_eps):
        offsets[h][e] = total
        total += counts[h][e]

print(f"Total requests: {total}, pool size: {total * 4 / (1024*1024):.2f} MB")

# Pass 2: Direct scatter into lat_pool
lat_pool = [0] * total
cur_pos = [list(offsets[h]) for h in range(72)]

for line in lines:
    hid = get_hour_idx(line)
    parts = line.split(b' ')
    ep = parts[2]
    eid = remap[ep_map[ep]]
    lat = int(parts[4])
    pos = cur_pos[hid][eid]
    lat_pool[pos] = lat
    cur_pos[hid][eid] += 1

print(f"Pass 2 scatter done in {time.perf_counter() - t0:.3f}s")

# Pass 3: Percentiles & output
out_lines = []
for h in range(72):
    hr_str = GLOBAL_HOURS[h]
    for e in range(num_eps):
        cnt = counts[h][e]
        if cnt == 0: continue
        err = errors[h][e]
        st = offsets[h][e]
        sub = sorted(lat_pool[st : st + cnt])
        
        idx50 = (50 * cnt + 99) // 100 - 1
        idx95 = (95 * cnt + 99) // 100 - 1
        idx99 = (99 * cnt + 99) // 100 - 1
        p50 = sub[idx50]
        p95 = sub[idx95]
        p99 = sub[idx99]
        ppm = err * 1000000 // cnt
        
        ep_str = sorted_ep_names[e].decode()
        out_lines.append(f"{hr_str} {ep_str} {cnt} {p50} {p95} {p99} {err} {ppm}\n".encode())

gen = b''.join(out_lines)
with open('data/a/summary.txt', 'rb') as f:
    ref = f.read()

gen_h = hashlib.sha256(gen).hexdigest()
ref_h = hashlib.sha256(ref).hexdigest()

print("Match:", gen_h == ref_h)
assert gen_h == ref_h
print(f"SUCCESS in {time.perf_counter() - t0:.3f}s total!")
