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
| 18 | Direct 64-bit FNV hashing + fast currency accumulation | run-58bfe0255846 | 0.114 | 44 | 56.02 |
| 19 | Parallel chunk split + SIMD quote jumping (BOM check regression on hostile CSV) | run-65735e1c2ad0 | 0.123 | 46 | failure (hash mismatch) |
| 20 | Restored UTF-8 BOM check on hostile CSV + 64KB direct O(1) 3-letter code lookup table for regions.csv + parallel boundary search | run-db09b1f9805b | 0.117 | 47 | 55.47 |
| 21 | Reused thread-local all_hashes buffer in Phase 2 deduplication to eliminate per-country allocations | run-388696ebcb91 | 0.117 | 49 | 55.33 |
| 22 | Direct pointer-subtraction string length + unquoted fast-path integer/cents parsing | run-9fa3fe19bcdf | 0.102 | 46 | 57.02 |
| 23 | Direct SIMD comma pointer subtraction on unquoted note/name (BOM check regression on hostile CSV) | run-38185f14e0bc | 0.103 | 49 | failure (hash mismatch) |
| 24 | Restored UTF-8 BOM check in parse_field_int + restored standard unquoted delimiters | run-1b5c2c22c2d0 | 0.103 | 49 | 56.78 |
| 25 | Open-addressing deduplication test (unbounded probe loop on full table) | run-bffbf6ef22db | timeout | 49 | failure (timeout) |
| 26 | Fast unescaped quote detection + consecutive hash filter + robust thread-local sort/unique deduplication | run-2b88d63da923 | 0.103 | 49 | 56.71 |
| 27 | DynamicNameSet per-country open-addressing hash set | run-22bb851fea32 | 0.144 | 72 | 51.16 |
| 28 | Restored proven consecutive filter with thread-local vector sort and unescaped quote fast-path | run-8713f6ab4b25 | 0.104 | 49 | 56.64 |
| 29 | Parallel chunk partitioning & fast unquoted comma scan consistency | run-05e6f08b7f16 | 0.101 | 48 | 56.98 |
| 30 | Consecutive filter with sort/unique deduplication consistency | run-42f059206bbc | 0.103 | 48 | 56.73 |
| 31 | Composite (country, hash) key deduplication eliminating 2,880 separate heap vectors | run-a0eb17a0302e | 0.132 | 44.1 | 54.50 |
| 32 | Single flat array sort + O(N) linear scan deduplication + incremental 256KB madvise eviction | run-b811bbc93003 | 0.426 | 38.6 | 42.28 |
| 33 | Restored proven parallel country hash deduplication from commit 05b3c62 | run-1791ba7ce50a | 0.104 | 48.8 | 56.74 |
| 34 | Parallel country deduplication consistency | run-33e52fda228b | 0.102 | 48.6 | 56.95 |
| 35 | Parallel country deduplication run consistency | run-02d8d38563ff | 0.104 | 48.5 | 56.77 |
| 36 | Hardware CRC32 name hashing + single-launch persistent thread barrier + sequential split points | run-3cb0627f62c9 | 0.101 | 49.6 | 56.71 |
| 37 | Hardware CRC32 + single barrier consistency | run-0bb817a66388 | 0.102 | 49.0 | 56.66 |
| 38 | Hardware CRC32 + single barrier consistency | run-6170f6cd4a66 | 0.112 | 49.2 | 55.66 |
| 39 | Hardware CRC32 + single barrier consistency | run-c0f87dad0c73 | 0.102 | 49.6 | 56.61 |
| 40 | Hardware CRC32 + single barrier consistency | run-d24ed829103c | 0.103 | 49.2 | 56.55 |
| 41 | Hardware CRC32 + single barrier consistency | run-4b70d9df7617 | 0.104 | 49.4 | 56.46 |
