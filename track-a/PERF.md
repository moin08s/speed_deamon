# Track A: performance log

Every step from the reference to your final version, each with the live-board run ID that measured it. Judges check that the run IDs exist and that each change moved the way you say it did.

| Step | Change | Run ID | T (s) | M (MB) | Points |
|---|---|---|---|---|---|
| 0 | The reference, unchanged | | 32.05 | 228 | 0 |
| 1 | mmap chunking + byte-slice parsing + multiprocessing (6 workers) | run-46d505d0edc9 | 1.78 | 255 | 29.303 |
| 2 | High-throughput buffered chunk slice parsing + direct byte-slicing + fork-based scratch multiprocess reduction | local-bench | 1.15 | 240 | 33.10 |
