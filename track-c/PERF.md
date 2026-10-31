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
