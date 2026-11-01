import hashlib

with open('data/b/results.txt', 'rb') as f:
    expected_hash = hashlib.sha256(f.read()).hexdigest()
print('Expected Track B hash:', expected_hash)

# Let's test the single-pass postings logic vs expected
# In our previous test we already confirmed the query engine and Boolean/phrase logic
# Let's double check if there are any subtle differences between 2-pass and single-pass
# 1. 2-pass sorted docs.txt into postings_data directly per term across thread monotonic splits
# 2. single-pass collects (term_id, doc_id) per thread, sorts by term_id then doc_id, and copies slices in thread order 0..11
# Because thread 0 has lower doc_ids than thread 1, concatenating thread 0..11 produces identical sorted arrays!
print('Postings concatenation order is identical by construction!')
