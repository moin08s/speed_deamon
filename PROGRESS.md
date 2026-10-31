# Speed Demon 2026: Overall Progress & Performance Log

This document tracks all four competition tracks (A, B, C, D), verifying correctness gates, timing performance against official reference baselines, and maintaining zero third-party dependencies ($D = 1.0$).

---

## Performance Summary Across Tracks

Team: **Cache Me If You Can**  
Language: Python 3.13 (Standard Library Only, 0 Dependencies)

| Track | Task | Official Reference | Previous Scored | Optimized Local Benchmark | Peak Memory | Expected Points | Status |
|---|---|---|---|---|---|---|---|
| **Track A** | Ingest (3.1M logs) | 32.05s | 1.78s (255 MB) | **1.15s** (~28× faster) | 240 MB | **~33.1** | **Counts** |
| **Track B** | Index (50k queries) | 56.35s | 31.88s (2,930 MB) | **7.20s** (~7.8× faster) | 380 MB | **~25.4** | **Counts** |
| **Track C** | Join (3.32M rows) | 14.44s | 10.56s (367 MB) | **3.20s** (~4.5× faster) | 340 MB | **~18.5** | Reserve / 4th Track |
| **Track D** | Traverse (5M edges) | 72.69s | 27.08s (1,352 MB) | **7.80s** (~9.3× faster) | 145 MB | **~29.8** | **Counts** |

---

## Architectural & Algorithmic Optimizations

### Track A: Ingest
- **Algorithm**: High-throughput buffered chunk slice parsing with multi-worker parallel map-reduce.
- **Optimizations**: Replaced repeated `mmap.find()` system calls with direct in-memory chunk slice buffer reads (`f.seek() + f.read()`), fast RFC3339 byte slicing (`line[:13]`), single-byte status checking (`b'5'`), and fork-based multiprocess reduction via scratch files in `/tmp`.
- **Dependencies**: 0 (Standard library only, $D = 1$).
- **Source Size**: ~112 lines.

### Track B: Index
- **Algorithm**: Selective Inverted Indexing with zero-allocation `match_phrase_fast` boundary scanning and fork-based multiprocess query engine.
- **Optimizations**:
  - Filtered inverted index storing only terms appearing in `queries.txt`.
  - Zero-RAM text retention: document positions stored in contiguous `array('Q')` 64-bit offsets into `mmap`.
  - `match_phrase_fast`: Zero-allocation substring + token boundary scanner using a fast 256-element byte lookup table (`IS_ALNUM`). Eliminates full-document regex tokenization on phrase query candidates.
  - Fast-path empty query formatting (`if not doc_ids: return "0 0"`).
  - Multi-worker parallel query evaluation across 6 CPU cores using `os.fork()` with shared Copy-on-Write memory, writing directly to scratch files in `/tmp` and streaming binary concatenation into `results.txt`.
- **Dependencies**: 0 (Standard library only, $D = 1$).
- **Source Size**: ~191 lines.

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
- **Algorithm**: Compressed Sparse Row (CSR) flat array graph representation + Bidirectional Dijkstra with early pruning and multi-worker batching.
- **Optimizations**:
  - CSR Representation: replaced 3.5 million Python list/tuple objects with contiguous flat arrays (`fwd_head`, `fwd_dst`, `fwd_w`, `bwd_head`, `bwd_dst`, `bwd_w` using `array('I')` and `array('Q')`). Reduces graph memory footprint by **90%** from 1,352 MB down to **~145 MB**.
  - Bidirectional Dijkstra with `nd < best` early pruning: skips edge traversal and heap insertions immediately when candidate distance equals or exceeds current best known path.
  - Multi-worker parallel query batching across 6 CPU cores using `os.fork()` with zero-overhead shared COW arrays, writing directly to `/tmp` scratch files and streaming concatenation into `distances.txt`.
- **Dependencies**: 0 (Standard library only, $D = 1$).
- **Source Size**: ~150 lines.
