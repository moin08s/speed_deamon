# Track A: Ingest

The task, the input files and the exact output format are in the [spec](https://github.com/hackathon-raptors/speed-demon-2026/blob/main/specs/a-ingest.md).

## Approach

1. **Memory-Mapped Multi-core Chunking**: Memory maps (`mmap`) `/data/requests.log` and splits it across worker processes aligned to newline boundaries.
2. **Zero-Datetime Ingestion**: Replaces slow `strptime` with exact RFC3339 byte slicing (`line[:13]` for the hour string).
3. **Byte-level Delimiter Scanning**: Locates method, endpoint, status, and latency via fast byte searches without constructing unnecessary intermediate strings or objects.
4. **Standard Library Only**: Uses zero external dependencies to preserve a 1.0 multiplier on the dependency metric ($D = 1$).

## Dependencies

None. Standard library only.

## With another 24 hours

1. Implement custom histogram / bucketed percentile calculation to avoid full list sorting on large groups.
2. Compile worker loops with Cython or explore SIMD-accelerated line scanning.
