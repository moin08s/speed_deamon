# Track B: performance log

Every step from the reference to your final version, each with the live-board run ID that measured it. Judges check that the run IDs exist and that each change moved the way you say it did.

| Step | Change | Run ID | T (s) | M (MB) | Points |
|---|---|---|---|---|---|
| 0 | The reference, unchanged | | 56.35 | 2290 | 0 |
| 1 | Selective query term indexing + parallel mmap corpus parsing | 9e19e61 | 45.3 | 3422 | 0 |
| 2 | Zero-RAM text retention (array offset indexing directly on mmap) | 76d70da | 57.9 | 2319 | 0 |
| 3 | Multiprocess parallel query evaluation across 4 worker cores with ordered reassembly | | | | |
