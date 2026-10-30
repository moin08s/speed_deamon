# Track B: Index

The task, the input files and the exact output format are in the [spec](https://github.com/hackathon-raptors/speed-demon-2026/blob/main/specs/b-index.md).

## Approach

1. **Selective Inverted Indexing**: Reads and analyzes all 50,000 queries first, building an index strictly for query terms rather than the entire 600k+ vocabulary.
2. **Parallel Corpus Parsing**: Divides `docs.txt` across worker processes using memory-mapped I/O aligned to newline boundaries.
3. **Frozenset Fast Boolean Operations**: Converts posting lists into frozensets for C-accelerated bitwise set operations (`&`, `|`, `-`).
4. **Early Exit Candidates for Phrases**: Intersects candidate document sets before scanning token sequences.
5. **Zero Third-Party Dependencies**: Standard library only ($D = 1$).

## Dependencies

None. Standard library only.

## With another 24 hours

1. Bitset/Roaring bitmap encoding for posting lists to accelerate intersections.
2. Pre-index phrase token pairs or positions for top phrase terms.
