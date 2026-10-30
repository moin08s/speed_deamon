# Speed Demon 2026: Overall Progress & Performance Log

This document tracks all four competition tracks (A, B, C, D), verifying correctness gates, timing performance against official reference baselines, and maintaining zero third-party dependencies.

---

## Track Summary Table

| Track | Task | Input | Output | Reference Time | Measured Time | Speedup Factor | Full Seed 1 Hash | Gate Status |
|---|---|---|---|---|---|---|---|---|
| **A** | Ingest | `requests.log` | `summary.txt` | 32.05s | **2.11s** | **~15.2×** | `495ca09682b9673ef836a506496177680bb4a3af54255174befddb1f83481cd3` | **PASS** |
| **B** | Index | `docs.txt`, `queries.txt` | `results.txt` | 56.35s | **~51s** | **~1.1×** | `f86973c210228141bcb6f1f1b10188ad2a9b65f71e7cd7cf4590b38de4e80927` | **PASS** |
| **C** | Join | `records.csv`, `regions.csv` | `aggregates.txt` | 14.44s | **11.84s** | **~1.2×** | `d8088b1cd42ba1338b1345b107b55db7b96ef1f5066f3ff64fd26a8912a0f536` | **PASS** |
| **D** | Traverse | `graph.txt`, `queries.txt` | `distances.txt` | 72.69s | **39.70s** | **~1.8×** | `a23dbdf0a3e49cc8901310a9ee1dc6a9a569a85297b6da75c2a7002cb2ed277e` | **PASS** |

---

## Detailed Track Records

### Track A: Ingest
- **Algorithm**: Memory-mapped (`mmap`) multiprocessing across 6 worker processes.
- **Optimizations**: RFC3339 byte slicing (`line[:13]`) replacing `datetime.strptime`, fast space scanning, single-byte status checking (`b'5'`).
- **Dependencies**: 0 (Standard library only, $D = 1$).
- **Source Size**: ~110 lines.

### Track B: Index
- **Algorithm**: Targeted Inverted Indexing reading `queries.txt` first.
- **Optimizations**: Only indexes terms present in queries (~11,430 terms vs 600,000 corpus vocabulary), C-accelerated `frozenset` bitwise operations, early-exit phrase candidate filtering.
- **Dependencies**: 0 (Standard library only, $D = 1$).
- **Source Size**: ~160 lines.

### Track C: Join
- **Algorithm**: Streaming CSV reader with direct dictionary join.
- **Optimizations**: Start and mid-file UTF-8 BOM (`\ufeff`) stripping, dot-index integer cents parsing, direct ASCII string length counting.
- **Dependencies**: 0 (Standard library only, $D = 1$).
- **Source Size**: ~93 lines.

### Track D: Traverse
- **Algorithm**: Bidirectional Dijkstra search on dual adjacency list.
- **Optimizations**: Expands from $s$ and $t$ simultaneously alternating smaller heaps, immediate early termination when $\min(\text{heap}_f) + \min(\text{heap}_b) \ge \text{best}$, handles all edge cases (unreachable `-1`, self-loops, parallel edges, $> 2^{32}-1$).
- **Dependencies**: 0 (Standard library only, $D = 1$).
- **Source Size**: ~86 lines.
