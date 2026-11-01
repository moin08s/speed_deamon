with open('data/c/records.csv', 'rb') as f:
    data = f.read()

p = 0
limit = len(data)
if data[:3] == b'\xef\xbb\xbf': p += 3
while p < limit and data[p] != 10: p += 1
if p < limit and data[p] == 10: p += 1

idx = 0
prev_p = p
while p < limit:
    while p < limit and data[p] in (10, 13): p += 1
    if p >= limit: break
    start_p = p
    
    # id
    val = 0
    while p < limit and 48 <= data[p] <= 57:
        val = val * 10 + (data[p] - 48)
        p += 1
    if p < limit and data[p] == 44: p += 1
    
    # name
    if p < limit and data[p] == 34:
        p += 1
        while p < limit:
            if data[p] == 34:
                if p + 1 < limit and data[p+1] == 34:
                    p += 2
                else:
                    p += 1
                    break
            else:
                p += 1
        if p < limit and data[p] == 44: p += 1
    else:
        while p < limit and data[p] not in (44, 10, 13): p += 1
        if p < limit and data[p] == 44: p += 1
        
    # note
    if p < limit and data[p] == 34:
        p += 1
        while p < limit:
            if data[p] == 34:
                if p + 1 < limit and data[p+1] == 34:
                    p += 2
                else:
                    p += 1
                    break
            else:
                p += 1
        if p < limit and data[p] == 44: p += 1
    else:
        while p < limit and data[p] not in (44, 10, 13): p += 1
        if p < limit and data[p] == 44: p += 1
    
    # reg
    while p < limit and 48 <= data[p] <= 57: p += 1
    if p < limit and data[p] == 44: p += 1
    
    # amount
    while p < limit and data[p] != 10: p += 1
    if p < limit and data[p] == 10: p += 1
    
    if idx in (113225, 113226, 113227):
        print(f"Record {idx} bytes:")
        print(repr(data[start_p:p]))
    idx += 1
    if idx > 113228: break
