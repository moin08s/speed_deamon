import hashlib

# Load and verify data/a/summary.txt
with open('data/a/summary.txt', 'rb') as f:
    orig = f.read()
    orig_sha = hashlib.sha256(orig).hexdigest()

print("Original summary sha256:", orig_sha)
print("Total lines:", len(orig.splitlines()))
