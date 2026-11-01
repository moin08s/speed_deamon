import time

with open('data/c/records.csv', 'rb') as f:
    data = f.read()

t0 = time.perf_counter()
q = data.count(b'"')
t1 = time.perf_counter()
print(f"Counting all quotes in 20MB: {(t1-t0)*1000:.3f} ms, found {q} quotes")
