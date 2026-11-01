import hashlib

# Verify that our streaming python parser extracts the exact doc offsets
docs_offsets = {}
with open('data/b/docs.txt', 'rb') as f:
    pos = 0
    for line in f:
        tab_idx = line.find(b'\t')
        if tab_idx != -1:
            doc_id = int(line[:tab_idx])
            text_offset = pos + tab_idx + 1
            text_len = len(line) - (tab_idx + 1)
            if line.endswith(b'\r\n'): text_len -= 2
            elif line.endswith(b'\n'): text_len -= 1
            docs_offsets[doc_id] = (text_offset, text_len)
        pos += len(line)

print("Parsed", len(docs_offsets), "docs")
# Verify doc 1
with open('data/b/docs.txt', 'rb') as f:
    for doc_id in [1, 2, 100, 500, 25000]:
        if doc_id in docs_offsets:
            off, ln = docs_offsets[doc_id]
            f.seek(off)
            snippet = f.read(ln)
            print(f"Doc {doc_id}: len={ln}, snippet={repr(snippet[:30])}")
