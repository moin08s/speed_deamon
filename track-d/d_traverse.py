#!/usr/bin/env python3
"""Optimized Track D (Traverse): Bidirectional Dijkstra with fast ASCII ingestion and early termination."""
import heapq
import os
import sys

GRAPH_FILE = "graph.txt"
QUERIES_FILE = "queries.txt"
OUT_FILE = "distances.txt"

def load_graph(path):
    with open(path, "r", encoding="ascii", buffering=2 * 1024 * 1024) as f:
        header = f.readline().split()
        n = int(header[0])
        fwd = [[] for _ in range(n)]
        bwd = [[] for _ in range(n)]
        for line in f:
            parts = line.split()
            u = int(parts[0])
            v = int(parts[1])
            w = int(parts[2])
            fwd[u].append((v, w))
            bwd[v].append((u, w))
    return fwd, bwd

def load_queries(path):
    with open(path, "r", encoding="ascii") as f:
        return [tuple(map(int, line.split())) for line in f]

def bidir_dijkstra(fwd, bwd, s, t):
    if s == t:
        return 0
    df = {s: 0}
    db = {t: 0}
    hf = [(0, s)]
    hb = [(0, t)]
    best = float("inf")

    while hf and hb:
        if hf[0][0] + hb[0][0] >= best:
            break

        if len(hf) <= len(hb):
            d, u = heapq.heappop(hf)
            if d > df[u]:
                continue
            for v, w in fwd[u]:
                nd = d + w
                if v not in df or nd < df[v]:
                    df[v] = nd
                    heapq.heappush(hf, (nd, v))
                    if v in db and nd + db[v] < best:
                        best = nd + db[v]
        else:
            d, u = heapq.heappop(hb)
            if d > db[u]:
                continue
            for v, w in bwd[u]:
                nd = d + w
                if v not in db or nd < db[v]:
                    db[v] = nd
                    heapq.heappush(hb, (nd, v))
                    if v in df and nd + df[v] < best:
                        best = nd + df[v]

    return best if best < float("inf") else -1

def main(data_dir, out_dir):
    fwd, bwd = load_graph(os.path.join(data_dir, GRAPH_FILE))
    queries = load_queries(os.path.join(data_dir, QUERIES_FILE))

    answers = [bidir_dijkstra(fwd, bwd, s, t) for s, t in queries]

    os.makedirs(out_dir, exist_ok=True)
    with open(os.path.join(out_dir, OUT_FILE), "w", encoding="ascii", newline="\n") as f:
        f.writelines(f"{d}\n" for d in answers)

if __name__ == "__main__":
    data_dir = sys.argv[1] if len(sys.argv) > 1 else "/data"
    out_dir = sys.argv[2] if len(sys.argv) > 2 else "/out"
    main(data_dir, out_dir)
