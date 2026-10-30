# Speed Demon 2026: Overall Progress & Performance Log

This document tracks all four competition tracks (A, B, C, D), verifying correctness gates, timing performance against official reference baselines, and maintaining zero third-party dependencies.

---

## Live Board Performance Summary

Team: **Cache Me If You Can**  
Latest Championship Score: **9.289** (Geometric Mean of best 3 tracks: A, D, B)

| Track | Task | Official Reference | Our Scored Time | Our Memory | Official Run ID | Scored Points | Status |
|---|---|---|---|---|---|---|---|
| **Track A** | Ingest (3.1M logs) | 32.05s | **1.78s** (~18× faster) | 255 MB | `run-46d505d0edc9` | **29.303** | **Counts** |
| **Track B** | Index (50k queries) | 56.35s | **31.88s** (~1.8× faster) | 2,930 MB | `run-5cc0db169683` | **3.729** | **Counts** |
| **Track C** | Join (3.32M rows) | 14.44s | **10.56s** (~1.4× faster) | 367 MB | `run-10fce46ce0d2` | **3.412** | Dropped (Weakest) |
| **Track D** | Traverse (5M edges) | 72.69s | **27.08s** (~2.7× faster) | 1,352 MB | `run-f249fd350c8c` | **7.336** | **Counts** |

---

## Detailed Track Records

### Track A: Ingest
- **Algorithm**: Memory-mapped (`mmap`) multiprocessing across 6 worker processes.
- **Optimizations**: RFC3339 byte slicing (`line[:13]`) replacing `datetime.strptime`, fast space scanning, single-byte status checking (`b'5'`).
- **Dependencies**: 0 (Standard library only, $D = 1$).
- **Source Size**: ~92 lines.

### Track B: Index
- **Algorithm**: Targeted Inverted Indexing reading `queries.txt` first with zero in-memory document strings.
- **Optimizations**: Replaced string duplication with `array('Q')` storing 64-bit byte offsets into `mmap`, C-accelerated `frozenset` bitwise operations, multi-worker parallel query evaluation across 4 cores with deterministic indexed reassembly.
- **Dependencies**: 0 (Standard library only, $D = 1$).
- **Source Size**: ~132 lines.

### Track C: Join
- **Algorithm**: High-throughput 2MB buffered CSV reader with direct dictionary join.
- **Optimizations**: Start and mid-file UTF-8 BOM (`\ufeff`) stripping, dot-index integer cents parsing, direct ASCII string length counting.
- **Dependencies**: 0 (Standard library only, $D = 1$).
- **Source Size**: ~76 lines.

### Track D: Traverse
- **Algorithm**: Bidirectional Dijkstra search on dual adjacency list with buffered ASCII ingestion.
- **Optimizations**: Expands from $s$ and $t$ simultaneously alternating smaller heaps, immediate early termination when $\min(\text{heap}_f) + \min(\text{heap}_b) \ge \text{best}$, handles all edge cases (unreachable `-1`, self-loops, parallel edges, $> 2^{32}-1$).
- **Dependencies**: 0 (Standard library only, $D = 1$).
- **Source Size**: ~70 lines.
