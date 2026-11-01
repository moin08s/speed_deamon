with open('data/c/records.csv', 'rb') as f:
    data = f.read()

def parse_full(use_new):
    p = 0
    limit = len(data)
    
    # header
    if limit - p >= 3 and data[p:p+3] == b'\xef\xbb\xbf': p += 3
    while p < limit and data[p] != 10: p += 1
    if p < limit and data[p] == 10: p += 1
    
    records = []
    
    while p < limit:
        while p < limit and data[p] in (10, 13): p += 1
        if p >= limit: break
        
        # rec_id with BOM check
        if limit - p >= 3 and data[p:p+3] == b'\xef\xbb\xbf': p += 3
        val = 0
        while p < limit and 48 <= data[p] <= 57:
            val = val * 10 + (data[p] - 48)
            p += 1
        if p < limit and data[p] == 44: p += 1
        rec_id = val
        
        # name
        name_len = 0
        name_h = 0xcbf29ce484222325
        if p < limit and data[p] == 34:
            p += 1
            while p < limit:
                if not use_new:
                    if data[p] == 34:
                        if p + 1 < limit and data[p+1] == 34:
                            name_h = ((name_h ^ 34) * 0x100000001b3) & 0xffffffffffffffff
                            name_len += 1
                            p += 2
                        else:
                            p += 1
                            break
                    else:
                        c = data[p]
                        p += 1
                        if (c & 0xc0) != 0x80: name_len += 1
                        name_h = ((name_h ^ c) * 0x100000001b3) & 0xffffffffffffffff
                else:
                    q = data.find(b'"', p, limit)
                    if q == -1:
                        for k in range(p, limit):
                            c = data[k]
                            if (c & 0xc0) != 0x80: name_len += 1
                            name_h = ((name_h ^ c) * 0x100000001b3) & 0xffffffffffffffff
                        p = limit
                        break
                    for k in range(p, q):
                        c = data[k]
                        if (c & 0xc0) != 0x80: name_len += 1
                        name_h = ((name_h ^ c) * 0x100000001b3) & 0xffffffffffffffff
                    if q + 1 < limit and data[q+1] == 34:
                        name_len += 1
                        name_h = ((name_h ^ 34) * 0x100000001b3) & 0xffffffffffffffff
                        p = q + 2
                    else:
                        p = q + 1
                        break
            if p < limit and data[p] == 44: p += 1
        else:
            if not use_new:
                while p < limit and data[p] not in (44, 10, 13):
                    c = data[p]
                    p += 1
                    if (c & 0xc0) != 0x80: name_len += 1
                    name_h = ((name_h ^ c) * 0x100000001b3) & 0xffffffffffffffff
                if p < limit and data[p] == 44: p += 1
            else:
                comma = data.find(b',', p, limit)
                end = comma if comma != -1 else limit
                for k in range(p, end):
                    if data[k] in (10, 13):
                        end = k
                        break
                    c = data[k]
                    if (c & 0xc0) != 0x80: name_len += 1
                    name_h = ((name_h ^ c) * 0x100000001b3) & 0xffffffffffffffff
                p = (comma + 1) if comma != -1 else end
                
        # note
        note_len = 0
        if p < limit and data[p] == 34:
            p += 1
            while p < limit:
                if not use_new:
                    if data[p] == 34:
                        if p + 1 < limit and data[p+1] == 34:
                            note_len += 1
                            p += 2
                        else:
                            p += 1
                            break
                    else:
                        c = data[p]
                        p += 1
                        if (c & 0xc0) != 0x80: note_len += 1
                else:
                    q = data.find(b'"', p, limit)
                    if q == -1:
                        for k in range(p, limit):
                            if (data[k] & 0xc0) != 0x80: note_len += 1
                        p = limit
                        break
                    for k in range(p, q):
                        if (data[k] & 0xc0) != 0x80: note_len += 1
                    if q + 1 < limit and data[q+1] == 34:
                        note_len += 1
                        p = q + 2
                    else:
                        p = q + 1
                        break
            if p < limit and data[p] == 44: p += 1
        else:
            if not use_new:
                while p < limit and data[p] not in (44, 10, 13):
                    c = data[p]
                    p += 1
                    if (c & 0xc0) != 0x80: note_len += 1
                if p < limit and data[p] == 44: p += 1
            else:
                comma = data.find(b',', p, limit)
                end = comma if comma != -1 else limit
                for k in range(p, end):
                    if data[k] in (10, 13):
                        end = k
                        break
                    if (data[k] & 0xc0) != 0x80: note_len += 1
                p = (comma + 1) if comma != -1 else end
                
        # reg_id
        reg_id = 0
        while p < limit and 48 <= data[p] <= 57:
            reg_id = reg_id * 10 + (data[p] - 48)
            p += 1
        if p < limit and data[p] == 44: p += 1
        
        # amount
        # skip to newline
        while p < limit and data[p] != 10: p += 1
        if p < limit and data[p] == 10: p += 1
        
        records.append((rec_id, name_h, name_len, note_len, reg_id))
    return records

r_old = parse_full(False)
r_new = parse_full(True)
print("Old records:", len(r_old))
print("New records:", len(r_new))
diffs = 0
for i in range(min(len(r_old), len(r_new))):
    if r_old[i] != r_new[i]:
        diffs += 1
        if diffs <= 5:
            print(f"Mismatch at index {i}:")
            print("Old:", r_old[i])
            print("New:", r_new[i])
if diffs == 0 and len(r_old) == len(r_new):
    print("ALL 100% IDENTICAL!")
