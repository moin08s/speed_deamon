with open('data/d/graph.txt') as f:
    n, m = map(int, f.readline().split())
    fwd_head = [0] * (n + 1)
    bwd_head = [0] * (n + 1)
    edges = []
    for line in f:
        u, v, w = map(int, line.split())
        edges.append((u, v, w))
        fwd_head[u + 1] += 1
        bwd_head[v + 1] += 1

for i in range(n):
    fwd_head[i + 1] += fwd_head[i]
    bwd_head[i + 1] += bwd_head[i]

fwd_dst = [0] * m
fwd_w = [0] * m
fwd_cur = list(fwd_head)
bwd_dst = [0] * m
bwd_w = [0] * m
bwd_cur = list(bwd_head)

for u, v, w in edges:
    pf = fwd_cur[u]
    fwd_cur[u] += 1
    fwd_dst[pf] = v
    fwd_w[pf] = w
    pb = bwd_cur[v]
    bwd_cur[v] += 1
    bwd_dst[pb] = u
    bwd_w[pb] = w

with open('data/d/queries.txt') as f:
    queries = [tuple(map(int, line.split())) for line in f if line.strip()]

with open('data/d/distances.txt') as f:
    expected = [int(line.strip()) for line in f if line.strip()]

import heapq

def solve_bidir_test(s, t):
    if s == t: return 0
    if s >= n or t >= n: return -1
    
    dist_f = {}
    dist_b = {}
    
    dist_f[s] = 0
    dist_b[t] = 0
    
    # 64-bit distance tuple
    hf = [(0, s)]
    hb = [(0, t)]
    
    best = float('inf')
    
    while hf and hb:
        if hf[0][0] + hb[0][0] >= best:
            break
            
        if len(hf) <= len(hb):
            d, u = heapq.heappop(hf)
            if d > dist_f.get(u, float('inf')) or d >= best:
                continue
            st = fwd_head[u]
            en = fwd_head[u + 1]
            for i in range(st, en):
                nd = d + fwd_w[i]
                if nd < best:
                    v = fwd_dst[i]
                    old_d = dist_f.get(v, float('inf'))
                    if old_d == float('inf') or nd < old_d:
                        dist_f[v] = nd
                        heapq.heappush(hf, (nd, v))
                        bv = dist_b.get(v, float('inf'))
                        if bv != float('inf'):
                            cand = nd + bv
                            if cand < best:
                                best = cand
        else:
            d, u = heapq.heappop(hb)
            if d > dist_b.get(u, float('inf')) or d >= best:
                continue
            st = bwd_head[u]
            en = bwd_head[u + 1]
            for i in range(st, en):
                nd = d + bwd_w[i]
                if nd < best:
                    v = bwd_dst[i]
                    old_d = dist_b.get(v, float('inf'))
                    if old_d == float('inf') or nd < old_d:
                        dist_b[v] = nd
                        heapq.heappush(hb, (nd, v))
                        fv = dist_f.get(v, float('inf'))
                        if fv != float('inf'):
                            cand = nd + fv
                            if cand < best:
                                best = cand
                                
    return -1 if best == float('inf') else best

mismatches = 0
for i, (s, t) in enumerate(queries):
    ans = solve_bidir_test(s, t)
    if ans != expected[i]:
        mismatches += 1
        print(f"Mismatch at query {i} ({s}->{t}): got {ans}, expected {expected[i]}")

print("Total mismatches on sample with 64-bit distances:", mismatches)
