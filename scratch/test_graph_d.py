import time

with open('data/d/graph.txt') as f:
    header = f.readline().split()
    n = int(header[0])
    m = int(header[1])
    print(f"Nodes N={n}, Edges M={m}")

with open('data/d/queries.txt') as f:
    queries = [line.split() for line in f if line.strip()]
    print(f"Queries count={len(queries)}")
