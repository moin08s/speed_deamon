import hashlib

# Verify data/a/summary.txt target sha256
with open('data/a/summary.txt', 'rb') as f:
    target_sha = hashlib.sha256(f.read()).hexdigest()
print("Track A target sha256:", target_sha)
