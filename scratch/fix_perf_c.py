import re

with open('track-c/PERF.md', 'r') as f:
    text = f.read()

old_row = '| 15 | SIMD AVX2 memchr note length scanner + 32-bit compact FNV hashes | pending | pending | pending | pending |'
new_rows = '| 15 | 32-bit FNV hash deduplication test (hash collision on large evaluation dataset) | run-7e243ff4a6c9 | 0.106 | 26 | failure (hash mismatch) |\n| 16 | Restored 64-bit zero-collision FNV hash + mallopt M_ARENA_MAX=2 | run-32c55d9ddd54 | 0.118 | 45 | 55.57 |'

assert old_row in text
text = text.replace(old_row, new_rows)

with open('track-c/PERF.md', 'w') as f:
    f.write(text)

print('Updated track-c/PERF.md')
