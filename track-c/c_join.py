#!/usr/bin/env python3.13
import csv
import os
import sys

RECORDS_FILE = "records.csv"
REGIONS_FILE = "regions.csv"
OUTPUT_FILE = "aggregates.txt"
UNMATCHED = "_UNMATCHED"

def parse_cents(amt_str: str) -> int:
    dot = amt_str.find(".")
    if amt_str.startswith("-"):
        return -(int(amt_str[1:dot]) * 100 + int(amt_str[dot + 1 :]))
    return int(amt_str[:dot]) * 100 + int(amt_str[dot + 1 :])

def format_cents(cents: int) -> str:
    sign = "-" if cents < 0 else ""
    cents = abs(cents)
    return f"{sign}{cents // 100}.{cents % 100:02d}"

def main(data_dir, out_dir):
    regions_path = os.path.join(data_dir, REGIONS_FILE)
    records_path = os.path.join(data_dir, RECORDS_FILE)

    # 1. Load regions lookup
    regions = {}
    with open(regions_path, "r", encoding="utf-8", newline="") as f:
        reader = csv.reader(f)
        next(reader)
        for r_id, country in reader:
            regions[int(r_id)] = country

    # 2. Stream records.csv and aggregate
    counts = {}
    def get_entry(k):
        e = counts.get(k)
        if e is None:
            counts[k] = e = [0, 0, 0, None, None, set(), 0, 0]
        return e

    with open(records_path, "r", encoding="utf-8", newline="") as f:
        reader = csv.reader(f)
        header = next(reader)
        for row in reader:
            if not row:
                continue
            id_str = row[0]
            if id_str.startswith("\ufeff"):
                id_str = id_str[1:]
            rec_id = int(id_str)
            name = row[1]
            note = row[2]
            reg_id = int(row[3])
            cents = parse_cents(row[4])

            country = regions.get(reg_id, UNMATCHED)
            entry = get_entry(country)
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

    # 3. Format output sorted by country key
    lines = []
    for k in sorted(counts.keys()):
        e = counts[k]
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
