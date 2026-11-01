with open('data/d/graph.txt') as f:
    n, m = map(int, f.readline().split())
    fwd = [[] for _ in range(n)]
    bwd = [[] for _ in range(n)]
    for line in f:
        u, v, w = map(int, line.split())
        fwd[u].append((v, w))
        bwd[v].append((u, w))

with open('data/d/queries.txt') as f:
    queries = [tuple(map(int, line.split())) for line in f if line.strip()]

import heapq

def run_bench(by_dist):
    total_pops = 0
    for s, t in queries:
        if s == t: continue
        dist_f = {s: 0}
        dist_b = {t: 0}
        hf = [(0, s)]
        hb = [(0, t)]
        best = float('inf')
        while hf and hb:
            if hf[0][0] + hb[0][0] >= best: break
            choose_f = (hf[0][0] <= hb[0][0]) if by_dist else (len(hf) <= len(hb))
            if choose_f:
                d, u = heapq.heappop(hf)
                total_pops += 1
                if d > dist_f[u] or d >= best: continue
                for v, w in fwd[u]:
                    nd = d + w
                    if nd < best and (v not in dist_f or nd < dist_f[v]):
                        dist_f[v] = nd
                        heapq.heappush(hf, (nd, v))
                        if v in dist_b:
                            cand = nd + dist_b[v]
                            if cand < best: best = cand
            else:
                d, u = heapq.heappop(hb)
                total_pops += 1
                if d > dist_b[u] or d >= best: continue
                for v, w in bwd[u]:
                    nd = d + w
                    if nd < best and (v not in dist_b or nd < dist_b[v]):
                        dist_b[v] = nd
                        heapq.heappush(hb, (nd, v))
                        if v in dist_f:
                            cand = nd + dist_f[v]
                            if cand < best: best = cand
    return total_pops

print("Total pops by dist:", run_bench(True))
print("Total pops by size:", run_bench(False))
