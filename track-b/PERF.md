# Track B: performance log

Every step from the reference to your final version, each with the live-board run ID that measured it. Judges check that the run IDs exist and that each change moved the way you say it did.

| Step | Change | Run ID | T (s) | M (MB) | Points |
|---|---|---|---|---|---|
| 0 | The reference, unchanged | | 56.35 | 2290 | 0 |
| 1 | Selective query term indexing + parallel mmap corpus parsing | run-03be4cfcb928 | 45.33 | 3422 | 0 |
| 2 | Zero-RAM text retention (array offset indexing directly on mmap) | run-0dda7abfd6c6 | 57.91 | 2319 | 0 |
| 3 | Multiprocess parallel query evaluation across 4 worker cores with deterministic ordered reassembly | run-5cc0db169683 | 31.88 | 2930 | 3.729 |
