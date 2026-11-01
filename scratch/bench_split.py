import time

with open('data/d/graph.txt', 'rb') as f:
    raw = f.read()

# Test parsing speed of 500k lines in Python
start = time.perf_counter()
lines = raw.splitlines()[1:]
count = len(lines)
t = time.perf_counter() - start
print(f"Split {count} lines in {t*1000:.2f} ms")
