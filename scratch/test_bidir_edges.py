import heapq, hashlib

# Load graph
with open('data/d/graph.txt') as f:
    header = f.readline().split()
    n, m = int(header[0]), int(header[1])
    fwd_head = [0] * (n + 1)
    bwd_head = [0] * (n + 1)
    fwd_deg = [0] * n
    bwd_deg = [0] * n
    edges = []
    for line in f:
        p = line.split()
        if len(p) >= 3:
            u, v, w = int(p[0]), int(p[1]), int(p[2])
            edges.append((u, v, w))
            fwd_deg[u] += 1
            bwd_deg[v] += 1

cur_f = 0
cur_b = 0
for i in range(n):
    fwd_head[i] = cur_f
    cur_f += fwd_deg[i]
    bwd_head[i] = cur_b
    cur_b += bwd_deg[i]
fwd_head[n] = cur_f
bwd_head[n] = cur_b

fwd_edges = [None] * cur_f
bwd_edges = [None] * cur_b
f_pos = list(fwd_head[:n])
b_pos = list(bwd_head[:n])

for u, v, w in edges:
    pf = f_pos[u]
    f_pos[u] += 1
    fwd_edges[pf] = (v, w)
    pb = b_pos[v]
    b_pos[v] += 1
    bwd_edges[pb] = (u, w)

# Load queries
with open('data/d/queries.txt') as f:
    queries = [[int(x) for x in line.split()[:2]] for line in f if line.strip()]

out_lines = []
for s, t in queries:
    if s == t:
        out_lines.append("0\n")
        continue
    
    dist_f = {}
    dist_b = {}
    hf = [(0, s)]
    hb = [(0, t)]
    dist_f[s] = 0
    dist_b[t] = 0
    best = float('inf')
    
    while hf and hb:
        if hf[0][0] + hb[0][0] >= best:
            break
        if len(hf) <= len(hb):
            d, u = heapq.heappop(hf)
            if d > dist_f.get(u, float('inf')) or d >= best: continue
            if u in dist_b:
                cand = d + dist_b[u]
                if cand < best: best = cand
            st = fwd_head[u]
            en = fwd_head[u + 1]
            for i in range(st, en):
                v, w = fwd_edges[i]
                nd = d + w
                if nd < best:
                    if v not in dist_f:
                        dist_f[v] = nd
                        heapq.heappush(hf, (nd, v))
                        if v in dist_b:
                            cand = nd + dist_b[v]
                            if cand < best: best = cand
                    elif nd < dist_f[v]:
                        dist_f[v] = nd
                        heapq.heappush(hf, (nd, v))
                        if v in dist_b:
                            cand = nd + dist_b[v]
                            if cand < best: best = cand
        else:
            d, u = heapq.heappop(hb)
            if d > dist_b.get(u, float('inf')) or d >= best: continue
            if u in dist_f:
                cand = d + dist_f[u]
                if cand < best: best = cand
            st = bwd_head[u]
            en = bwd_head[u + 1]
            for i in range(st, en):
                v, w = bwd_edges[i]
                nd = d + w
                if nd < best:
                    if v not in dist_b:
                        dist_b[v] = nd
                        heapq.heappush(hb, (nd, v))
                        if v in dist_f:
                            cand = nd + dist_f[v]
                            if cand < best: best = cand
                    elif nd < dist_b[v]:
                        dist_b[v] = nd
                        heapq.heappush(hb, (nd, v))
                        if v in dist_f:
                            cand = nd + dist_f[v]
                            if cand < best: best = cand

    ans = -1 if best == float('inf') else best
    out_lines.append(f"{ans}\n")

calc_sha = hashlib.sha256("".join(out_lines).encode()).hexdigest()
with open('data/d/distances.txt', 'rb') as f:
    expected_sha = hashlib.sha256(f.read()).hexdigest()

print("Calculated sha256:", calc_sha)
print("Expected   sha256:", expected_sha)
print("Match:", calc_sha == expected_sha)
