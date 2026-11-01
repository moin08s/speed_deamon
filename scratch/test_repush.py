import heapq

# Load graph
with open('data/d/graph.txt') as f:
    header = f.readline().split()
    n, m = int(header[0]), int(header[1])
    adj = [[] for _ in range(n)]
    radj = [[] for _ in range(n)]
    for line in f:
        p = line.split()
        if len(p) >= 3:
            u, v, w = int(p[0]), int(p[1]), int(p[2])
            adj[u].append((v, w))
            radj[v].append((u, w))

# Load queries
with open('data/d/queries.txt') as f:
    queries = [[int(x) for x in line.split()[:2]] for line in f if line.strip()]

first_push = 0
re_push = 0

for s, t in queries[:20]:
    dist_f = {}
    dist_b = {}
    hf = [(0, s)]
    dist_f[s] = 0
    while hf:
        d, u = heapq.heappop(hf)
        if d > dist_f.get(u, float('inf')): continue
        for v, w in adj[u]:
            nd = d + w
            if v not in dist_f:
                first_push += 1
                dist_f[v] = nd
                heapq.heappush(hf, (nd, v))
            elif nd < dist_f[v]:
                re_push += 1
                dist_f[v] = nd
                heapq.heappush(hf, (nd, v))

print(f"First push: {first_push}, Re-push: {re_push} ({re_push/(first_push+re_push)*100:.2f}%)")
