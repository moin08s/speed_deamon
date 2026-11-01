# Test parallel CSR construction logic against sequential
import numpy as np

with open('data/d/graph.txt') as f:
    header = f.readline().split()
    N = int(header[0])
    M = int(header[1])

# Sequential
fwd_deg = [0] * N
bwd_deg = [0] * N
edges = []
with open('data/d/graph.txt') as f:
    f.readline()
    for line in f:
        u, v, w = map(int, line.split())
        fwd_deg[u] += 1
        bwd_deg[v] += 1
        edges.append((u, v, w))

print(f'N={N}, M={M}, Read {len(edges)} edges.')
print(f'Max fwd deg={max(fwd_deg)}, Max bwd deg={max(bwd_deg)}')
