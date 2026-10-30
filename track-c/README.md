# Track C: Ingest & Join

The task, the input files and the exact output format are in the [spec](https://github.com/hackathon-raptors/speed-demon-2026/blob/main/specs/c-join.md).

## Approach

1. **Compliant CSV Parsing**: Relies on standard library `csv.reader` configured for streaming records, correctly handling all hostile CSV features: quoted commas, escaped quotes (`""`), embedded multiline newlines, and mixed CRLF/LF.
2. **Robust BOM Handling**: Gracefully strips starting and mid-file UTF-8 BOM (`\ufeff`) prefixes from record IDs.
3. **Exact Integer Arithmetic**: Parses monetary amounts directly into 64-bit integer cents using fast dot indexing, avoiding all floating-point roundoff issues.
4. **UTF-8 Byte Length Optimization**: Capitalizes on the spec constraint that non-BOM bytes are ASCII (making `len(s)` identical to UTF-8 byte length), avoiding redundant UTF-8 re-encoding of strings.
5. **Zero Third-Party Dependencies**: Standard library only ($D = 1$).

## Dependencies

None. Standard library only.

## With another 24 hours

1. Multi-threaded custom chunk parser using SIMD quote-mask scanning.
2. Parallel streaming aggregation across worker processes.
