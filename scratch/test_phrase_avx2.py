with open('data/b/queries.txt', 'r') as f:
    queries = f.readlines()

with open('data/b/results.txt', 'r') as f:
    results = f.readlines()

print(f"Total queries: {len(queries)}, total results: {len(results)}")
assert len(queries) == len(results)
print("Lengths match perfectly!")
