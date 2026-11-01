import hashlib, time

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

# Pass 1: Parse and store into flat records
with open('data/a/requests.log', 'rb') as f:
    lines = f.readlines()

ep_map = {}
ep_list = []

# flat records: (hour, ep_id, lat, is_err)
records = []
for line in lines:
    hid = get_hour_idx(line)
    parts = line.split(b' ')
    ep = parts[2]
    status = parts[3]
    lat = int(parts[4])
    is_err = 1 if status[0] == 53 else 0
    
    eid = ep_map.get(ep)
    if eid is None:
        eid = len(ep_list)
        ep_map[ep] = eid
        ep_list.append(ep)
    records.append((hid, eid, lat, is_err))

# Global endpoint sort
sorted_eids = sorted(range(len(ep_list)), key=lambda i: ep_list[i])
remap = [0] * len(ep_list)
for new_id, old_id in enumerate(sorted_eids):
    remap[old_id] = new_id

sorted_ep_names = [ep_list[i] for i in sorted_eids]
num_eps = len(sorted_ep_names)

# Partition by hour
hour_records = [[] for _ in range(72)]
for hid, old_eid, lat, is_err in records:
    new_eid = remap[old_eid]
    hour_records[hid].append((new_eid, lat, is_err))

out_lines = []
for h in range(72):
    recs = hour_records[h]
    if not recs: continue
    
    # Counting sort recs by new_eid
    ep_counts = [0] * num_eps
    ep_errors = [0] * num_eps
    for eid, lat, is_err in recs:
        ep_counts[eid] += 1
        if is_err: ep_errors[eid] += 1
        
    ep_offsets = [0] * (num_eps + 1)
    for i in range(num_eps):
        ep_offsets[i + 1] = ep_offsets[i] + ep_counts[i]
        
    cur_pos = list(ep_offsets)
    lat_pool = [0] * len(recs)
    for eid, lat, is_err in recs:
        p = cur_pos[eid]
        lat_pool[p] = lat
        cur_pos[eid] += 1
        
    hr_str = GLOBAL_HOURS[h]
    for eid in range(num_eps):
        cnt = ep_counts[eid]
        if cnt == 0: continue
        err = ep_errors[eid]
        st = ep_offsets[eid]
        en = ep_offsets[eid + 1]
        
        # Subslice
        sub = sorted(lat_pool[st:en])
        idx50 = (50 * cnt + 99) // 100 - 1
        idx95 = (95 * cnt + 99) // 100 - 1
        idx99 = (99 * cnt + 99) // 100 - 1
        p50 = sub[idx50]
        p95 = sub[idx95]
        p99 = sub[idx99]
        ppm = err * 1000000 // cnt
        line = f"{hr_str} {sorted_ep_names[eid].decode()} {cnt} {p50} {p95} {p99} {err} {ppm}\n"
        out_lines.append(line.encode('ascii'))

full_out = b"".join(out_lines)
sim_hash = hashlib.sha256(full_out).hexdigest()
with open('data/a/summary.txt', 'rb') as f:
    ref_hash = hashlib.sha256(f.read()).hexdigest()

print(f"Time: {time.perf_counter() - t0:.3f}s")
print("Simulated SHA-256:", sim_hash)
print("Reference SHA-256:", ref_hash)
print("MATCH:", sim_hash == ref_hash)
assert sim_hash == ref_hash
