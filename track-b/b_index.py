#!/usr/bin/env python3.13
import mmap
import multiprocessing as mp
import os
import re
import sys

DOCS_FILE = "docs.txt"
QUERIES_FILE = "queries.txt"
RESULTS_FILE = "results.txt"

TOKEN_RE = re.compile(rb"[a-z0-9]+")
EMPTY = frozenset()

def tokenize(text: bytes):
    return TOKEN_RE.findall(text.lower())

def run_bool(index, words):
    res = index.get(words[0], EMPTY)
    for op, term in zip(words[1::2], words[2::2]):
        post = index.get(term, EMPTY)
        if op == b"AND":
            res = res & post
        elif op == b"OR":
            res = res | post
        elif op == b"NOT":
            res = res - post
    return res

def run_phrase(index, texts, terms):
    cands = index.get(terms[0], EMPTY)
    for t in terms[1:]:
        cands = cands & index.get(t, EMPTY)
        if not cands:
            return []

    k = len(terms)
    matches = []
    for doc_id in cands:
        toks = tokenize(texts[doc_id])
        for i in range(len(toks) - k + 1):
            if toks[i : i + k] == terms:
                matches.append(doc_id)
                break
    return matches

def format_result(doc_ids):
    ids = sorted(doc_ids)
    m = min(10, len(ids))
    parts = [str(len(ids)), str(sum(ids))]
    for i in range(m):
        parts.append(str(ids[i]))
    return " ".join(parts)

def parse_worker(file_path, start, end, needed_terms, q):
    local_index = {t: [] for t in needed_terms}
    local_texts = []
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
                id_bytes, _, text = line.partition(b"\t")
                doc_id = int(id_bytes)
                local_texts.append((doc_id, text))
                toks = set(TOKEN_RE.findall(text.lower()))
                for t in toks.intersection(needed_terms):
                    local_index[t].append(doc_id)
    q.put((local_index, local_texts))

def main(data_dir, out_dir):
    docs_path = os.path.join(data_dir, DOCS_FILE)
    queries_path = os.path.join(data_dir, QUERIES_FILE)

    raw_queries = []
    needed_terms = set()
    with open(queries_path, "rb") as f:
        for line in f:
            line = line.rstrip(b"\r\n")
            if not line:
                continue
            raw_queries.append(line)
            kind, _, rest = line.partition(b" ")
            if kind == b"BOOL":
                parts = rest.split(b" ")
                for t in parts[0::2]:
                    needed_terms.add(t)
            elif kind == b"PHRASE":
                for t in tokenize(rest):
                    needed_terms.add(t)

    num_workers = 6
    total_size = os.path.getsize(docs_path)
    chunk_size = total_size // num_workers
    ranges = []
    with open(docs_path, "rb") as f:
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
        p = mp.Process(target=parse_worker, args=(docs_path, st, en, needed_terms, q))
        p.start()
        procs.append(p)

    index = {t: set() for t in needed_terms}
    doc_tuples = []
    for _ in range(num_workers):
        sub_index, sub_texts = q.get()
        doc_tuples.extend(sub_texts)
        for t, ids in sub_index.items():
            if ids:
                index[t].update(ids)

    for p in procs:
        p.join()

    frozen_index = {t: frozenset(ids) for t, ids in index.items() if ids}
    doc_tuples.sort(key=lambda x: x[0])
    texts = [t[1] for t in doc_tuples]

    results = []
    for line in raw_queries:
        kind, _, rest = line.partition(b" ")
        if kind == b"BOOL":
            res = run_bool(frozen_index, rest.split(b" "))
            results.append(format_result(res) + "\n")
        elif kind == b"PHRASE":
            terms = tokenize(rest)
            res = run_phrase(frozen_index, texts, terms)
            results.append(format_result(res) + "\n")

    os.makedirs(out_dir, exist_ok=True)
    out_file = os.path.join(out_dir, RESULTS_FILE)
    with open(out_file, "w", encoding="utf-8", newline="\n") as f:
        f.writelines(results)

if __name__ == "__main__":
    data_dir = sys.argv[1] if len(sys.argv) > 1 else "/data"
    out_dir = sys.argv[2] if len(sys.argv) > 2 else "/out"
    main(data_dir, out_dir)
