import heapq, hashlib

# Read graph
with open('data/d/graph.txt', 'r') as f:
    line = f.readline().split()
    n, m = int(line[0]), int(line[1])
    fwd_adj = [[] for _ in range(n)]
    bwd_adj = [[] for _ in range(n)]
    for line in f:
        if not line.strip(): continue
        parts = line.split()
        u, v, w = int(parts[0]), int(parts[1]), int(parts[2])
        if u < n and v < n:
            fwd_adj[u].append((v, w))
            bwd_adj[v].append((u, w))

# Read queries
queries = []
with open('data/d/queries.txt', 'r') as f:
    for line in f:
        if not line.strip(): continue
        p = line.split()
        queries.append((int(p[0]), int(p[1])))

def solve_bidir(s, t):
    if s == t: return 0
    if s >= n or t >= n: return -1

    dist_f = {}
    dist_b = {}
    dist_f[s] = 0
    dist_b[t] = 0

    hf = [(0, s)]
    hb = [(0, t)]

    best = float('inf')

    while hf and hb:
        min_f, _ = hf[0]
        min_b, _ = hb[0]
        if min_f + min_b >= best:
            break

        # alternate by min key
        if min_f <= min_b:
            d, u = heapq.heappop(hf)
            if d > dist_f[u] or d >= best:
                continue
            if u in dist_b:
                best = min(best, d + dist_b[u])
            for v, w in fwd_adj[u]:
                nd = d + w
                if nd < best:
                    if v not in dist_f or nd < dist_f[v]:
                        dist_f[v] = nd
                        heapq.heappush(hf, (nd, v))
                        if v in dist_b:
                            best = min(best, nd + dist_b[v])
        else:
            d, u = heapq.heappop(hb)
            if d > dist_b[u] or d >= best:
                continue
            if u in dist_f:
                best = min(best, d + dist_f[u])
            for v, w in bwd_adj[u]:
                nd = d + w
                if nd < best:
                    if v not in dist_b or nd < dist_b[v]:
                        dist_b[v] = nd
                        heapq.heappush(hb, (nd, v))
                        if v in dist_f:
                            best = min(best, nd + dist_f[v])

    return best if best != float('inf') else -1

results = []
for s, t in queries:
    ans = solve_bidir(s, t)
    results.append(str(ans) + '\n')

output_bytes = "".join(results).encode('utf-8')
got_hash = hashlib.sha256(output_bytes).hexdigest()
print("Got hash:     ", got_hash)
with open('data/d/distances.txt', 'rb') as f:
    expected_hash = hashlib.sha256(f.read()).hexdigest()
print("Expected hash:", expected_hash)
assert got_hash == expected_hash, "MISMATCH!"
print("MATCH 100.000% EXACT!")
