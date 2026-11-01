import hashlib

with open('data/b/docs.txt', 'r', encoding='utf-8') as f:
    docs_lines = f.readlines()

with open('data/b/queries.txt', 'r', encoding='utf-8') as f:
    queries_lines = f.readlines()

# Verify that 1-term queries, all-AND queries, and phrase queries behave identically
with open('data/b/results.txt', 'rb') as f:
    expected_results = f.read()

print("Reference hash:", hashlib.sha256(expected_results).hexdigest())
