#!/usr/bin/env python3
import mmap
import multiprocessing as mp
import os
import sys

INPUT_NAME = "requests.log"
OUTPUT_NAME = "summary.txt"

def _parse_worker(file_path, start, end, q):
    groups = {}
    with open(file_path, "rb") as f:
        with mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ) as mm:
            idx = start
            while idx < end:
                nl = mm.find(b"\n", idx, end)
                if nl == -1:
                    line = mm[idx:end]
                    idx = end
                else:
                    line = mm[idx:nl]
                    idx = nl + 1
                if not line:
                    continue

                hour = line[:13]
                sp2 = line.find(b" ", 29)
                sp3 = line.find(b" ", sp2 + 1)
                sp4 = line.find(b" ", sp3 + 1)

                endpoint = line[sp2 + 1 : sp3]
                status_char = line[sp3 + 1]
                latency = int(line[sp4 + 1 :])

                key = (hour, endpoint)
                grp = groups.get(key)
                if grp is None:
                    groups[key] = grp = [[], 0]
                grp[0].append(latency)
                if status_char == 53: # b'5'
                    grp[1] += 1
    q.put(groups)

def main(data_dir, out_dir):
    file_path = os.path.join(data_dir, INPUT_NAME)
    total_size = os.path.getsize(file_path)

    num_workers = 6
    chunk_size = total_size // num_workers
    ranges = []

    with open(file_path, "rb") as f:
        with mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ) as mm:
            curr = 0
            for i in range(num_workers):
                if i == num_workers - 1:
                    nxt = total_size
                else:
                    nxt = mm.find(b"\n", curr + chunk_size) + 1
                ranges.append((curr, nxt))
                curr = nxt

    q = mp.Queue()
    procs = []
    for st, en in ranges:
        p = mp.Process(target=_parse_worker, args=(file_path, st, en, q))
        p.start()
        procs.append(p)

    merged = {}
    for _ in range(num_workers):
        res = q.get()
        for k, v in res.items():
            m = merged.get(k)
            if m is None:
                merged[k] = v
            else:
                m[0].extend(v[0])
                m[1] += v[1]

    for p in procs:
        p.join()

    out = []
    for key in sorted(merged.keys()):
        hour, endpoint = key
        lat_list, errs = merged[key]
        lat_list.sort()
        n = len(lat_list)
        p50 = lat_list[(50 * n + 99) // 100 - 1]
        p95 = lat_list[(95 * n + 99) // 100 - 1]
        p99 = lat_list[(99 * n + 99) // 100 - 1]
        ppm = errs * 1_000_000 // n
        out.append(f"{hour.decode()}:00:00Z {endpoint.decode()} {n} {p50} {p95} {p99} {errs} {ppm}\n")

    os.makedirs(out_dir, exist_ok=True)
    with open(os.path.join(out_dir, OUTPUT_NAME), "w", encoding="utf-8", newline="\n") as f:
        f.writelines(out)

if __name__ == "__main__":
    data_dir = sys.argv[1] if len(sys.argv) > 1 else "/data"
    out_dir = sys.argv[2] if len(sys.argv) > 2 else "/out"
    main(data_dir, out_dir)
