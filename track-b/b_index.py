#!/usr/bin/env python3
"""Speed Demon 2026: Track B (Index) Two-Phase Multi-Worker Parallel Indexing and Query Answering."""
import array
import mmap
import os
import pickle
import re
import shutil
import sys

DOCS_FILE = "docs.txt"
QUERIES_FILE = "queries.txt"
RESULTS_FILE = "results.txt"

TOKEN_RE = re.compile(rb"[a-z0-9]+")
EMPTY = frozenset()

IS_ALNUM = bytearray(256)
for c in b"abcdefghijklmnopqrstuvwxyz0123456789":
    IS_ALNUM[c] = 1

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

def match_phrase_fast(text: bytes, terms: list[bytes]) -> bool:
    text_lower = text.lower()
    L = len(text_lower)
    first_term = terms[0]
    len_first = len(first_term)

    pos = 0
    while pos < L:
        idx = text_lower.find(first_term, pos)
        if idx == -1:
            return False

        if idx > 0 and IS_ALNUM[text_lower[idx - 1]]:
            pos = idx + 1
            continue

        end_idx = idx + len_first
        if end_idx < L and IS_ALNUM[text_lower[end_idx]]:
            pos = idx + 1
            continue

        cur_pos = end_idx
        matched_all = True
        for t in terms[1:]:
            while cur_pos < L and not IS_ALNUM[text_lower[cur_pos]]:
                cur_pos += 1
            if cur_pos >= L:
                matched_all = False
                break
            len_t = len(t)
            if cur_pos + len_t > L or text_lower[cur_pos : cur_pos + len_t] != t:
                matched_all = False
                break
            after_t = cur_pos + len_t
            if after_t < L and IS_ALNUM[text_lower[after_t]]:
                matched_all = False
                break
            cur_pos = after_t

        if matched_all:
            return True
        pos = idx + 1
    return False

def run_phrase(index, mm, offsets, terms):
    cands = index.get(terms[0], EMPTY)
    for t in terms[1:]:
        cands = cands & index.get(t, EMPTY)
        if not cands:
            return []

    matches = []
    for doc_id in sorted(cands):
        st = offsets[doc_id * 2]
        ln = offsets[doc_id * 2 + 1]
        if match_phrase_fast(mm[st : st + ln], terms):
            matches.append(doc_id)
    return matches

def format_result(doc_ids):
    if not doc_ids:
        return "0 0"
    ids = sorted(doc_ids) if not isinstance(doc_ids, list) else doc_ids
    m = min(10, len(ids))
    parts = [str(len(ids)), str(sum(ids))]
    for i in range(m):
        parts.append(str(ids[i]))
    return " ".join(parts)

def answer_query_line(line, frozen_index, mm, offsets):
    kind, _, rest = line.partition(b" ")
    if kind == b"BOOL":
        res = run_bool(frozen_index, rest.split(b" "))
        return format_result(res)
    elif kind == b"PHRASE":
        terms = TOKEN_RE.findall(rest.lower())
        res = run_phrase(frozen_index, mm, offsets, terms)
        return format_result(res)
    return "0 0"

def parse_doc_chunk(file_path, st, en, needed_terms):
    index = {}
    offsets = array.array("Q")
    with open(file_path, "rb") as f:
        with mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ) as mm:
            idx = st
            while idx < en:
                nl = mm.find(b"\n", idx, en)
                line_end = en if nl == -1 else nl
                tab = mm.find(b"\t", idx, line_end)
                if tab != -1:
                    doc_id = int(mm[idx:tab])
                    text_start = tab + 1
                    text_len = line_end - text_start
                    offsets.append(doc_id)
                    offsets.append(text_start)
                    offsets.append(text_len)
                    for t in set(TOKEN_RE.findall(mm[text_start:line_end].lower())) & needed_terms:
                        s = index.get(t)
                        if s is None:
                            index[t] = s = []
                        s.append(doc_id)
                idx = line_end + 1
    return offsets, index

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
                for t in TOKEN_RE.findall(rest.lower()):
                    needed_terms.add(t)

    with open(docs_path, "rb") as f:
        with mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ) as mm:
            total_size = len(mm)
            num_workers = 6
            chunk_size = total_size // num_workers
            ranges = []
            curr = 0
            for i in range(num_workers):
                if i == num_workers - 1:
                    nxt = total_size
                else:
                    nxt = mm.find(b"\n", curr + chunk_size) + 1
                ranges.append((curr, nxt))
                curr = nxt

    if hasattr(os, "fork") and total_size > 1_000_000:
        pids = []
        part_paths = []
        for w, (st, en) in enumerate(ranges):
            part_path = f"/tmp/b_idx_{w}.pkl"
            part_paths.append(part_path)
            pid = os.fork()
            if pid == 0:
                res = parse_doc_chunk(docs_path, st, en, needed_terms)
                with open(part_path, "wb") as pf:
                    pickle.dump(res, pf, protocol=pickle.HIGHEST_PROTOCOL)
                os._exit(0)
            pids.append(pid)

        for pid in pids:
            os.waitpid(pid, 0)

        parts = []
        for part_path in part_paths:
            with open(part_path, "rb") as pf:
                parts.append(pickle.load(pf))
            try:
                os.remove(part_path)
            except OSError:
                pass
    else:
        parts = [parse_doc_chunk(docs_path, st, en, needed_terms) for st, en in ranges]

    total_docs = sum(len(p[0]) // 3 for p in parts)
    offsets = array.array("Q", [0] * (2 * total_docs))
    merged_index = {}
    for off_chunk, idx_chunk in parts:
        for i in range(0, len(off_chunk), 3):
            doc_id = off_chunk[i]
            offsets[doc_id * 2] = off_chunk[i + 1]
            offsets[doc_id * 2 + 1] = off_chunk[i + 2]
        for t, ids in idx_chunk.items():
            s = merged_index.get(t)
            if s is None:
                merged_index[t] = s = []
            s.extend(ids)

    frozen_index = {t: frozenset(ids) for t, ids in merged_index.items()}
    del merged_index, parts

    total_q = len(raw_queries)
    os.makedirs(out_dir, exist_ok=True)
    out_file = os.path.join(out_dir, RESULTS_FILE)

    if hasattr(os, "fork") and total_q > 500:
        q_workers = 6
        chunk_size = (total_q + q_workers - 1) // q_workers
        pids = []
        part_paths = []
        for w in range(q_workers):
            start = w * chunk_size
            end = min(total_q, start + chunk_size)
            if start >= end:
                break
            part_path = f"/tmp/b_part_{w}.txt"
            part_paths.append(part_path)
            pid = os.fork()
            if pid == 0:
                worker_lines = []
                with open(docs_path, "rb") as f:
                    with mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ) as mm:
                        for q_idx in range(start, end):
                            worker_lines.append(
                                answer_query_line(raw_queries[q_idx], frozen_index, mm, offsets) + "\n"
                            )
                with open(part_path, "w", encoding="ascii", newline="\n") as pf:
                    pf.writelines(worker_lines)
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
        results = []
        with open(docs_path, "rb") as f:
            with mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ) as mm:
                for line in raw_queries:
                    results.append(answer_query_line(line, frozen_index, mm, offsets) + "\n")
        with open(out_file, "w", encoding="ascii", newline="\n") as f:
            f.writelines(results)

if __name__ == "__main__":
    data_dir = sys.argv[1] if len(sys.argv) > 1 else "/data"
    out_dir = sys.argv[2] if len(sys.argv) > 2 else "/out"
    main(data_dir, out_dir)
