import heapq

with open('data/d/graph.txt') as f:
    header = f.readline().split()
    n, m = int(header[0]), int(header[1])
    fwd = [[] for _ in range(n)]
    bwd = [[] for _ in range(n)]
    for line in f:
        p = line.split()
        if len(p) >= 3:
            u, v, w = int(p[0]), int(p[1]), int(p[2])
            fwd[u].append((v, w))
            bwd[v].append((u, w))

with open('data/d/queries.txt') as f:
    queries = [[int(x) for x in line.split()[:2]] for line in f if line.strip()]

visited_nodes = []
for s, t in queries[:50]:
    if s == t: continue
    hf = [(0, s)]
    hb = [(0, t)]
    df = {s: 0}
    db = {t: 0}
    best = float('inf')
    visited = 0
    while hf and hb:
        if hf[0][0] + hb[0][0] >= best: break
        visited += 1
        if len(hf) <= len(hb):
            d, u = heapq.heappop(hf)
            if d > df.get(u, float('inf')) or d >= best: continue
            if u in db:
                cand = d + db[u]
                if cand < best: best = cand
            for v, w in fwd[u]:
                nd = d + w
                if nd < best:
                    if v not in df or nd < df[v]:
                        df[v] = nd
                        heapq.heappush(hf, (nd, v))
                        if v in db:
                            cand = nd + db[v]
                            if cand < best: best = cand
        else:
            d, u = heapq.heappop(hb)
            if d > db.get(u, float('inf')) or d >= best: continue
            if u in df:
                cand = d + df[u]
                if cand < best: best = cand
            for v, w in bwd[u]:
                nd = d + w
                if nd < best:
                    if v not in db or nd < db[v]:
                        db[v] = nd
                        heapq.heappush(hb, (nd, v))
                        if v in df:
                            cand = nd + df[v]
                            if cand < best: best = cand
    visited_nodes.append(visited)

print(f"Queries: {len(visited_nodes)}")
print(f"Avg visited: {sum(visited_nodes)/len(visited_nodes):.1f}")
print(f"Max visited: {max(visited_nodes)}")
print(f"Min visited: {min(visited_nodes)}")
