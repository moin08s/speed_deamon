# Speed Demon 2026: Overall Progress & Performance Log

This document tracks all four competition tracks (A, B, C, D), verifying correctness gates, timing performance against official reference baselines, and maintaining zero third-party dependencies ($D = 1.0$).

---

## Performance Summary Across Tracks

Team: **Cache Me If You Can**  
Language: Python 3.13 (Standard Library Only, 0 Dependencies)

| Track | Task | Official Reference | Previous Scored | Optimized Benchmark | Peak Memory | Expected Points | Status |
|---|---|---|---|---|---|---|---|
| **Track A** | Ingest (3.1M logs) | 32.05s | 1.78s (255 MB) | **1.15s** (~28× faster) | 240 MB | **~33.1** | **Counts** |
| **Track B** | Index (50k queries) | 56.35s | 31.88s (2,930 MB) | **4.80s** (~11.7× faster) | 350 MB | **~28.5** | **Counts** |
| **Track C** | Join (3.32M rows) | 14.44s | 10.56s (367 MB) | **3.20s** (~4.5× faster) | 340 MB | **~18.5** | Reserve / 4th Track |
| **Track D** | Traverse (5M edges) | 72.69s | 27.08s (1,352 MB) | **7.20s** (~10.1× faster) | 110 MB | **~32.1** | **Counts** |

---

## Championship Score Projection
Geometric Mean of top 3 tracks (A, D, B):
$$\text{Projected Score} = (33.1 \times 32.1 \times 28.5)^{1/3} \approx \mathbf{31.17}$$
(Up from previous live-board score of **9.289**).

---

## Architectural & Algorithmic Optimizations

### Track A: Ingest
- **Algorithm**: High-throughput buffered chunk slice parsing with multi-worker parallel map-reduce.
- **Optimizations**: Replaced repeated `mmap.find()` system calls with direct in-memory chunk slice buffer reads (`f.seek() + f.read()`), fast RFC3339 byte slicing (`line[:13]`), single-byte status checking (`b'5'`), and fork-based multiprocess reduction via scratch files in `/tmp`.
- **Dependencies**: 0 (Standard library only, $D = 1$).
- **Source Size**: ~112 lines.

### Track B: Index
- **Algorithm**: Two-phase multi-worker parallel indexing and query engine with zero-allocation `match_phrase_fast`.
- **Optimizations**:
  - Phase 1: Parallel corpus indexing across 6 worker processes using `os.fork()`, partitioning `docs.txt` into contiguous chunks and merging postings lists in $O(1)$ amortized time.
  - Phase 2: Parallel query evaluation across 6 worker processes with shared Copy-on-Write memory, streaming directly to `/tmp/b_part_{w}.txt` and performing fast binary concatenation.
  - `match_phrase_fast`: Zero-allocation substring + token boundary scanner using a fast 256-element byte lookup table (`IS_ALNUM`). Eliminates full-document regex tokenization on phrase query candidates.
  - Zero-RAM text retention: document positions stored in contiguous `array('Q')` 64-bit offsets into `mmap`.
  - Fast-path empty query formatting (`if not doc_ids: return "0 0"`).
- **Dependencies**: 0 (Standard library only, $D = 1$).
- **Source Size**: ~195 lines.

### Track C: Join
- **Algorithm**: Multi-worker parallel CSV parser with quote-parity chunk splitting.
- **Optimizations**:
  - Quote-parity chunk boundary splitting: computes exact record boundaries outside quotes by verifying even quote counts (`buf[:idx].count(b'"') % 2 == 0`), enabling safe multi-core CSV parsing across 6 CPU cores.
  - Fast integer cents parsing using C-level `s.replace('.', '')` followed by single `int()` conversion (more than 2.2× faster than string slicing and arithmetic).
  - Pre-indexed country dictionary: eliminates per-row dictionary insertion/function call overhead via direct key subscripting.
  - Mid-file BOM handling via fast index checking (`id_str[0] == '\ufeff'`).
- **Dependencies**: 0 (Standard library only, $D = 1$).
- **Source Size**: ~149 lines.

### Track D: Traverse
- **Algorithm**: Ultra-compact Compressed Sparse Row (CSR) flat array graph representation + Bidirectional Dijkstra with early pruning and multi-worker batching.
- **Optimizations**:
  - Ultra-compact CSR Representation: replaced 3.5 million Python list/tuple objects with contiguous flat arrays (`fwd_head`, `fwd_dst`, `fwd_w`, `bwd_head`, `bwd_dst`, `bwd_w` using `array('I')` for both heads, destinations, and weights). Reduces graph memory footprint by **10×** from 1,352 MB down to **~110 MB**.
  - Bidirectional Dijkstra with `nd < best` early pruning: skips edge traversal and heap insertions immediately when candidate distance equals or exceeds current best known path.
  - Multi-worker parallel query batching across 6 CPU cores using `os.fork()` with zero-overhead shared COW arrays, writing directly to `/tmp` scratch files and streaming concatenation into `distances.txt`.
- **Dependencies**: 0 (Standard library only, $D = 1$).
- **Source Size**: ~150 lines.
