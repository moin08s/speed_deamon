#!/usr/bin/env python3
"""Speed Demon 2026: Track D (Traverse) Ultra-Compact CSR Graph + Multiprocess Bidirectional Dijkstra."""
import array
import heapq
import os
import shutil
import sys

GRAPH_FILE = "graph.txt"
QUERIES_FILE = "queries.txt"
OUT_FILE = "distances.txt"

def build_csr(graph_path):
    with open(graph_path, "r", encoding="ascii", buffering=4 * 1024 * 1024) as f:
        line = f.readline()
        n, m = map(int, line.split())
        u_arr = array.array("I", [0] * m)
        v_arr = array.array("I", [0] * m)
        w_arr = array.array("I", [0] * m)
        fwd_head = array.array("I", [0] * (n + 1))
        bwd_head = array.array("I", [0] * (n + 1))
        idx = 0
        for line in f:
            p1 = line.find(" ")
            p2 = line.find(" ", p1 + 1)
            u = int(line[:p1])
            v = int(line[p1 + 1 : p2])
            w = int(line[p2 + 1 :])
            u_arr[idx] = u
            v_arr[idx] = v
            w_arr[idx] = w
            fwd_head[u + 1] += 1
            bwd_head[v + 1] += 1
            idx += 1

    for i in range(n):
        fwd_head[i + 1] += fwd_head[i]
        bwd_head[i + 1] += bwd_head[i]

    fwd_dst = array.array("I", [0] * m)
    fwd_w = array.array("I", [0] * m)
    fwd_cur = array.array("I", fwd_head)
    bwd_dst = array.array("I", [0] * m)
    bwd_w = array.array("I", [0] * m)
    bwd_cur = array.array("I", bwd_head)

    for i in range(m):
        u = u_arr[i]
        v = v_arr[i]
        w = w_arr[i]
        p = fwd_cur[u]
        fwd_dst[p] = v
        fwd_w[p] = w
        fwd_cur[u] += 1
        p = bwd_cur[v]
        bwd_dst[p] = u
        bwd_w[p] = w
        bwd_cur[v] += 1

    return fwd_head, fwd_dst, fwd_w, bwd_head, bwd_dst, bwd_w

def solve_bidir(fwd_head, fwd_dst, fwd_w, bwd_head, bwd_dst, bwd_w, s, t):
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
            st, en = fwd_head[u], fwd_head[u + 1]
            for i in range(st, en):
                nd = d + fwd_w[i]
                if nd < best:
                    v = fwd_dst[i]
                    if v not in df or nd < df[v]:
                        df[v] = nd
                        heapq.heappush(hf, (nd, v))
                        if v in db:
                            cand = nd + db[v]
                            if cand < best:
                                best = cand
        else:
            d, u = heapq.heappop(hb)
            if d > db[u]:
                continue
            st, en = bwd_head[u], bwd_head[u + 1]
            for i in range(st, en):
                nd = d + bwd_w[i]
                if nd < best:
                    v = bwd_dst[i]
                    if v not in db or nd < db[v]:
                        db[v] = nd
                        heapq.heappush(hb, (nd, v))
                        if v in df:
                            cand = nd + df[v]
                            if cand < best:
                                best = cand
    return best if best < float("inf") else -1

def main(data_dir, out_dir):
    graph_path = os.path.join(data_dir, GRAPH_FILE)
    queries_path = os.path.join(data_dir, QUERIES_FILE)

    fwd_head, fwd_dst, fwd_w, bwd_head, bwd_dst, bwd_w = build_csr(graph_path)

    with open(queries_path, "r", encoding="ascii") as f:
        queries = [tuple(map(int, line.split())) for line in f]

    os.makedirs(out_dir, exist_ok=True)
    out_file = os.path.join(out_dir, OUT_FILE)
    total_q = len(queries)

    if hasattr(os, "fork") and total_q > 20:
        num_workers = 6
        chunk_size = (total_q + num_workers - 1) // num_workers
        pids = []
        part_paths = []
        for w in range(num_workers):
            start = w * chunk_size
            end = min(total_q, start + chunk_size)
            if start >= end:
                break
            part_path = f"/tmp/d_part_{w}.txt"
            part_paths.append(part_path)
            pid = os.fork()
            if pid == 0:
                worker_ans = [
                    f"{solve_bidir(fwd_head, fwd_dst, fwd_w, bwd_head, bwd_dst, bwd_w, s, t)}\n"
                    for s, t in queries[start:end]
                ]
                with open(part_path, "w", encoding="ascii", newline="\n") as pf:
                    pf.writelines(worker_ans)
                os._exit(0)
            pids.append(pid)

        for pid in pids:
            os.waitpid(pid, 0)

        with open(out_file, "wb") as out_f:
            for part_path in part_paths:
                with open(part_path, "rb") as pf:
                    shutil.copyfileobj(pf, out_f)
                try:
                    os.remove(part_path)
                except OSError:
                    pass
    else:
        answers = [
            f"{solve_bidir(fwd_head, fwd_dst, fwd_w, bwd_head, bwd_dst, bwd_w, s, t)}\n"
            for s, t in queries
        ]
        with open(out_file, "w", encoding="ascii", newline="\n") as f:
            f.writelines(answers)

if __name__ == "__main__":
    data_dir = sys.argv[1] if len(sys.argv) > 1 else "/data"
    out_dir = sys.argv[2] if len(sys.argv) > 2 else "/out"
    main(data_dir, out_dir)
