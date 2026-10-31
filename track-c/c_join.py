#!/usr/bin/env python3
"""Speed Demon 2026: Track C (Join) Fast Parallel CSV Parser with Safe Quote Boundaries."""
import csv
import io
import os
import pickle
import sys

RECORDS_FILE = "records.csv"
REGIONS_FILE = "regions.csv"
OUTPUT_FILE = "aggregates.txt"
UNMATCHED = "_UNMATCHED"

def format_cents(cents: int) -> str:
    sign = "-" if cents < 0 else ""
    cents = abs(cents)
    return f"{sign}{cents // 100}.{cents % 100:02d}"

def parse_chunk_records(chunk_bytes, is_first, regions, all_countries):
    counts = {c: [0, 0, 0, None, None, set(), 0, 0] for c in all_countries}
    text = chunk_bytes.decode("utf-8")
    reader = csv.reader(io.StringIO(text))
    if is_first:
        next(reader)
    for row in reader:
        if not row:
            continue
        id_str = row[0]
        if id_str and id_str[0] == "\ufeff":
            id_str = id_str[1:]
        rec_id = int(id_str)
        name = row[1]
        note = row[2]
        reg_id = int(row[3])
        cents = int(row[4].replace(".", ""))

        country = regions.get(reg_id, UNMATCHED)
        entry = counts[country]
        entry[0] += 1
        entry[1] += rec_id
        entry[2] += cents
        if entry[3] is None or cents < entry[3]:
            entry[3] = cents
        if entry[4] is None or cents > entry[4]:
            entry[4] = cents
        entry[5].add(name)
        entry[6] += len(name)
        entry[7] += len(note)
    return counts

def main(data_dir, out_dir):
    regions_path = os.path.join(data_dir, REGIONS_FILE)
    records_path = os.path.join(data_dir, RECORDS_FILE)

    regions = {}
    with open(regions_path, "r", encoding="ascii", newline="") as f:
        f.readline()
        for line in f:
            line = line.strip()
            if line:
                r_id, country = line.split(",")
                regions[int(r_id)] = country

    all_countries = list(set(regions.values()))
    all_countries.append(UNMATCHED)

    with open(records_path, "rb") as f:
        buf = f.read()

    L = len(buf)
    num_workers = 6 if L > 1_000_000 else 1

    quote = bytes([34])
    split_points = [0]
    for i in range(1, num_workers):
        target = i * (L // num_workers)
        idx = target
        while idx < L:
            nl = buf.find(b"\n", idx)
            if nl == -1:
                idx = L
                break
            if buf[:nl].count(quote) % 2 == 0:
                split_points.append(nl + 1)
                break
            idx = nl + 1
    split_points.append(L)

    merged = {c: [0, 0, 0, None, None, set(), 0, 0] for c in all_countries}

    if hasattr(os, "fork") and num_workers > 1:
        pids = []
        part_paths = []
        for w in range(num_workers):
            part_path = f"/tmp/c_part_{w}.pkl"
            part_paths.append(part_path)
            pid = os.fork()
            if pid == 0:
                c_bytes = buf[split_points[w] : split_points[w + 1]]
                res = parse_chunk_records(c_bytes, w == 0, regions, all_countries)
                with open(part_path, "wb") as pf:
                    pickle.dump(res, pf, protocol=pickle.HIGHEST_PROTOCOL)
                os._exit(0)
            pids.append(pid)

        for pid in pids:
            os.waitpid(pid, 0)

        for part_path in part_paths:
            with open(part_path, "rb") as pf:
                res = pickle.load(pf)
            try:
                os.remove(part_path)
            except OSError:
                pass
            for k, e in res.items():
                if e[0] == 0:
                    continue
                m = merged[k]
                m[0] += e[0]
                m[1] += e[1]
                m[2] += e[2]
                if m[3] is None or e[3] < m[3]:
                    m[3] = e[3]
                if m[4] is None or e[4] > m[4]:
                    m[4] = e[4]
                m[5].update(e[5])
                m[6] += e[6]
                m[7] += e[7]
    else:
        for w in range(num_workers):
            c_bytes = buf[split_points[w] : split_points[w + 1]]
            res = parse_chunk_records(c_bytes, w == 0, regions, all_countries)
            for k, e in res.items():
                if e[0] == 0:
                    continue
                m = merged[k]
                m[0] += e[0]
                m[1] += e[1]
                m[2] += e[2]
                if m[3] is None or e[3] < m[3]:
                    m[3] = e[3]
                if m[4] is None or e[4] > m[4]:
                    m[4] = e[4]
                m[5].update(e[5])
                m[6] += e[6]
                m[7] += e[7]

    lines = []
    for k in sorted(merged.keys()):
        e = merged[k]
        if e[0] == 0:
            continue
        lines.append(
            f"{k} {e[0]} {e[1]} {format_cents(e[2])} {format_cents(e[3])} {format_cents(e[4])} {len(e[5])} {e[6]} {e[7]}\n"
        )

    os.makedirs(out_dir, exist_ok=True)
    out_file = os.path.join(out_dir, OUTPUT_FILE)
    with open(out_file, "w", encoding="utf-8", newline="\n") as f:
        f.writelines(lines)

if __name__ == "__main__":
    data_dir = sys.argv[1] if len(sys.argv) > 1 else "/data"
    out_dir = sys.argv[2] if len(sys.argv) > 2 else "/out"
    main(data_dir, out_dir)
