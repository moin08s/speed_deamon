# Track C: performance log

Every step from the reference to your final version, each with the live-board run ID that measured it. Judges check that the run IDs exist and that each change moved the way you say it did.

| Step | Change | Run ID | T (s) | M (MB) | Points |
|---|---|---|---|---|---|
| 0 | The reference, unchanged | | 14.44 | 400 | 0 |
| 1 | Streamed CSV parsing + fast dot-index cents + direct ASCII length calculation | run-10fce46ce0d2 | 10.56 | 367 | 3.412 |
| 2 | High-throughput 2MB buffered file streaming + direct ASCII region file indexing | run-a3dc483abfcb | 10.99 | 365 | 2.989 |
| 3 | Quote-parity chunk boundary splitting + multi-process parallel chunk parsing across 6 CPU cores + C-level `replace('.', '')` cents parsing + pre-indexed country mapping | run-e82769771777 | 3.89 | 2085 | 5.50 |
| 4 | Native C++20 implementation: POSIX mmap CSV parsing, 6 worker threads, direct flat array region-to-country lookup table, zero external dependencies | run-0027b91ca45f | 1.87 | 471 | 15.36 |
| 5 | Single-pass O(N) quote scan + 64-bit FNV name hashing + in-vector deduplication + flat region-to-country direct lookup table | run-0c8b2c109fd7 | 0.30 | 74 | 43.32 |
| 6 | 12-thread parallel country stats consolidation, hash set union deduplication, and buffered 1MB file streaming | run-f616def5a725 | 0.18 | 73 | 48.48 |
| 7 | In-place mmap regions parsing with 1024-slot CountryMap, parallel 12-thread per-country name hash collection + consolidation, immediate mmap unmap, fast buffer output | run-2cce13f9c122 | 0.168 | 70 | 49.98 |
| 8 | Canonical alphabetical pre-indexing + parallel 12-thread per-country L1 cache reduction + MADV_DONTNEED immediate chunk page cache eviction + direct parallel thread string formatting | run-462128a0c66b | 0.171 | 48 | 51.40 |
| 9 | Streaming 256KB madvise page eviction in parse loop + glibc single-arena restriction (mallopt M_ARENA_MAX=1) | run-1788cb47745a | 0.177 | 45 | 51.15 |
| 10 | Robust two-level parallel deduplication (thread-local L1 sort + Phase 2 merge sort) with small-count bypass | run-2079be2556fa | 0.200 | 47 | 49.85 |
| 11 | Robust two-level deduplication + canonical alphabetical indexing | run-bb72ae47e296 | 0.168 | 47 | 51.84 |
| 12 | O(1) localized fast-split boundary detection (eliminating 20MB full quote scan) | run-db8940e39e58 | 0.131 | 44 | 54.41 |
| 13 | Single-vector packed 64-bit country-hash entries + O(1) direct slice offsets in Phase 2 | run-dca4ecbd8a7c | 0.148 | 38 | 53.66 |
| 14 | High-throughput direct per-country deduplication + mallopt M_ARENA_MAX=2 | run-32c55d9ddd54 | 0.118 | 45 | 55.57 |
| 15 | 32-bit FNV hash deduplication test (hash collision on large evaluation dataset) | run-7e243ff4a6c9 | 0.106 | 26 | failure (hash mismatch) |
| 16 | Restored 64-bit zero-collision FNV hash + mallopt M_ARENA_MAX=2 | run-32c55d9ddd54 | 0.118 | 45 | 55.57 |
| 17 | Synchronized 12-thread per-country reduction + localized O(1) split | run-6de6f679cdfd | 0.115 | 45 | 55.82 |
