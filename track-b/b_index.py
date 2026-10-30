#!/usr/bin/env python3
"""Speed Demon 2026: Track B (Index) Fast Multi-Worker Query Answering with Strict Output Ordering."""
import array
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

def run_phrase(index, mm, offsets, terms):
    cands = index.get(terms[0], EMPTY)
    for t in terms[1:]:
        cands = cands & index.get(t, EMPTY)
        if not cands:
            return []

    k = len(terms)
    matches = []
    for doc_id in sorted(cands):
        st = offsets[doc_id * 2]
        ln = offsets[doc_id * 2 + 1]
        toks = tokenize(mm[st : st + ln])
        for i in range(len(toks) - k + 1):
            if toks[i : i + k] == terms:
                matches.append(doc_id)
                break
    return matches

def format_result(doc_ids):
    ids = sorted(doc_ids) if not isinstance(doc_ids, list) else doc_ids
    m = min(10, len(ids))
    parts = [str(len(ids)), str(sum(ids))]
    for i in range(m):
        parts.append(str(ids[i]))
    return " ".join(parts)

def query_worker(chunk_idx, docs_path, offsets, frozen_index, query_chunk, q):
    results = []
    with open(docs_path, "rb") as f:
        with mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ) as mm:
            for line in query_chunk:
                kind, _, rest = line.partition(b" ")
                if kind == b"BOOL":
                    res = run_bool(frozen_index, rest.split(b" "))
                    results.append(format_result(res) + "\n")
                elif kind == b"PHRASE":
                    terms = tokenize(rest)
                    res = run_phrase(frozen_index, mm, offsets, terms)
                    results.append(format_result(res) + "\n")
    q.put((chunk_idx, results))

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

    index = {t: set() for t in needed_terms}
    offsets = array.array("Q")

    with open(docs_path, "rb") as f:
        with mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ) as mm:
            idx = 0
            L = len(mm)
            while idx < L:
                nl = mm.find(b"\n", idx)
                line_end = L if nl == -1 else nl
                tab = mm.find(b"\t", idx, line_end)
                if tab != -1:
                    text_start = tab + 1
                    text_len = line_end - text_start
                    doc_id = len(offsets) // 2
                    offsets.append(text_start)
                    offsets.append(text_len)

                    toks = set(TOKEN_RE.findall(mm[text_start:line_end].lower()))
                    for t in toks.intersection(needed_terms):
                        index[t].add(doc_id)
                idx = line_end + 1

    frozen_index = {t: frozenset(ids) for t, ids in index.items() if ids}
    del index

    num_workers = 4
    total_q = len(raw_queries)
    chunk_size = (total_q + num_workers - 1) // num_workers
    chunks = [raw_queries[i : i + chunk_size] for i in range(0, total_q, chunk_size)]

    q = mp.Queue()
    procs = []
    for i, c in enumerate(chunks):
        p = mp.Process(target=query_worker, args=(i, docs_path, offsets, frozen_index, c, q))
        p.start()
        procs.append(p)

    results_dict = {}
    for _ in chunks:
        idx, res = q.get()
        results_dict[idx] = res

    for p in procs:
        p.join()

    # Reassemble strictly in input order
    flattened = []
    for i in range(len(chunks)):
        flattened.extend(results_dict[i])

    os.makedirs(out_dir, exist_ok=True)
    out_file = os.path.join(out_dir, RESULTS_FILE)
    with open(out_file, "w", encoding="utf-8", newline="\n") as f:
        f.writelines(flattened)

if __name__ == "__main__":
    data_dir = sys.argv[1] if len(sys.argv) > 1 else "/data"
    out_dir = sys.argv[2] if len(sys.argv) > 2 else "/out"
    main(data_dir, out_dir)
