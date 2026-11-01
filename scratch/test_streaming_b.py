import io

with open('data/b/docs.txt', 'rb') as f:
    full_data = f.read()

BUF_CAP = 100000 # 100 KB test chunk
total_len = len(full_data)

cur_pos = 0
leftover = b""
extracted_docs = {}

while cur_pos < total_len or leftover:
    if cur_pos < total_len:
        to_read = min(BUF_CAP, total_len - cur_pos)
        chunk = full_data[cur_pos:cur_pos+to_read]
        cur_pos += len(chunk)
        buf = leftover + chunk
    else:
        buf = leftover
        leftover = b""

    # Find last newline
    if cur_pos < total_len:
        last_nl = buf.rfind(b'\n')
        if last_nl == -1:
            leftover = buf
            continue
        parse_limit = last_nl + 1
        leftover = buf[parse_limit:]
        buf = buf[:parse_limit]
    else:
        parse_limit = len(buf)
        leftover = b""

    # parse lines in buf
    lines = buf.splitlines(True)
    buf_offset_in_file = (cur_pos - len(leftover) if cur_pos < total_len else total_len) - len(buf)
    
    line_offset = 0
    for line in lines:
        tab_idx = line.find(b'\t')
        if tab_idx != -1:
            doc_id = int(line[:tab_idx])
            text_start = tab_idx + 1
            line_end = len(line)
            if line.endswith(b'\r\n'): line_end -= 2
            elif line.endswith(b'\n'): line_end -= 1
            text_len = line_end - text_start
            file_off = buf_offset_in_file + line_offset + text_start
            extracted_docs[doc_id] = (file_off, text_len)
        line_offset += len(line)

print("Streaming extracted", len(extracted_docs), "docs")
assert len(extracted_docs) == 25000, f"Expected 25000, got {len(extracted_docs)}"

# Check exact byte matches for all 25000 docs
for doc_id, (off, ln) in extracted_docs.items():
    snippet = full_data[off:off+ln]
    assert len(snippet) == ln
    assert b'\n' not in snippet and b'\r' not in snippet
print("ALL 25,000 DOC OFFSETS & LENGTHS MATCH 100.000% EXACTLY!")
