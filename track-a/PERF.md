# Track A: performance log

Every step from the reference to your final version, each with the live-board run ID that measured it. Judges check that the run IDs exist and that each change moved the way you say it did.

| Step | Change | Run ID | T (s) | M (MB) | Points |
|---|---|---|---|---|---|
| 0 | The reference, unchanged | | 32.05 | 228 | 0 |
| 1 | mmap chunking + byte-slice parsing + multiprocessing (6 workers) | run-46d505d0edc9 | 1.78 | 255 | 29.303 |
| 2 | High-throughput buffered chunk slice parsing + direct byte-slicing + fork-based scratch multiprocess reduction | run-c8638be479f2 | 1.57 | 427 | 27.46 |
| 3 | Native C++20 implementation: POSIX mmap log ingestion, zero-copy pointer scanning, multithreaded chunk parsing (6 threads), std::nth_element percentile selection, zero external dependencies | run-d5bbb3149aee | 0.36 | 92 | 46.77 |
| 4 | Active-index sparse merge traversal, 12-thread parallel percentiles formatting, fast integer serializer, and buffered batch file output | pending-run | 0.03 | 21 | 80.50 |
