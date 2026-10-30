# Track B: Index

The task, the input files and the exact output format are in the [spec](https://github.com/hackathon-raptors/speed-demon-2026/blob/main/specs/b-index.md).

## Approach

1. **Selective Inverted Indexing**: Reads and analyzes all 50,000 queries first, building an index strictly for query terms rather than the entire 600k+ vocabulary.
2. **Zero-RAM Document Storage**: Replaced storing 250,000 in-memory string copies with a compact 64-bit array (`array('Q')`) storing byte offsets into a memory-mapped `docs.txt`, eliminating over 3 GB of RAM bloat.
3. **Multiprocess Parallel Query Engine**: Partitions the 50,000 queries across 4 worker processes with deterministic ordered reassembly, evaluating boolean logic and candidate phrase verifications concurrently across CPU cores.
4. **Frozenset Fast Boolean Operations**: Evaluates boolean query chains using C-accelerated bitwise set operations (`&`, `|`, `-`).
5. **Zero Third-Party Dependencies**: Standard library only ($D = 1$).

## Dependencies

None. Standard library only.

## With another 24 hours

1. Bitset/Roaring bitmap encoding for posting lists to accelerate intersections.
2. Pre-index phrase token pairs or positions for top phrase terms.
