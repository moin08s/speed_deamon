import time

# Measure reading data/d/graph.txt
start = time.perf_counter()
with open('data/d/graph.txt', 'rb') as f:
    data = f.read()
read_time = time.perf_counter() - start

print(f"Read 8MB in {read_time*1000:.2f} ms")
