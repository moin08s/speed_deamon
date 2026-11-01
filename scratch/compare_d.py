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

with open('data/d/distances.txt') as f:
    expected = [int(line.strip()) for line in f if line.strip()]

import heapq, time

def run_forward():
    t0 = time.perf_counter()
    pops = 0
    correct = 0
    for i, (s, t) in enumerate(queries):
        if s == t:
            correct += 1
            continue
        dist = {s: 0}
        heap = [(0, s)]
        ans = -1
        while heap:
            d, u = heapq.heappop(heap)
            pops += 1
            if d > dist[u]: continue
            if u == t:
                ans = d
                break
            for v, w in fwd[u]:
                nd = d + w
                if v not in dist or nd < dist[v]:
                    dist[v] = nd
                    heapq.heappush(heap, (nd, v))
        if ans == expected[i]:
            correct += 1
        else:
            print(f"Mismatch at {i}: got {ans}, expected {expected[i]}")
            break
    dt = time.perf_counter() - t0
    return correct, pops, dt

def run_bidir():
    t0 = time.perf_counter()
    pops = 0
    correct = 0
    for i, (s, t) in enumerate(queries):
        if s == t:
            correct += 1
            continue
        dist_f = {s: 0}
        dist_b = {t: 0}
        hf = [(0, s)]
        hb = [(0, t)]
        best = float('inf')
        while hf and hb:
            if hf[0][0] + hb[0][0] >= best: break
            if len(hf) <= len(hb):
                d, u = heapq.heappop(hf)
                pops += 1
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
                pops += 1
                if d > dist_b[u] or d >= best: continue
                for v, w in bwd[u]:
                    nd = d + w
                    if nd < best and (v not in dist_b or nd < dist_b[v]):
                        dist_b[v] = nd
                        heapq.heappush(hb, (nd, v))
                        if v in dist_f:
                            cand = nd + dist_f[v]
                            if cand < best: best = cand
        ans = -1 if best == float('inf') else best
        if ans == expected[i]:
            correct += 1
        else:
            print(f"Mismatch at {i}: got {ans}, expected {expected[i]}")
            break
    dt = time.perf_counter() - t0
    return correct, pops, dt

c_f, p_f, t_f = run_forward()
print(f"Forward Dijkstra: {c_f}/{len(queries)} correct, total pops: {p_f}, time: {t_f:.3f}s")
c_b, p_b, t_b = run_bidir()
print(f"Bidir Dijkstra:   {c_b}/{len(queries)} correct, total pops: {p_b}, time: {t_b:.3f}s")
