with open('data/c/records.csv', 'rb') as f:
    data = f.read()

p = 0
limit = len(data)

# skip header
while p < limit and data[p] != 10: p += 1
if p < limit and data[p] == 10: p += 1

mismatches = 0
total = 0

while p < limit:
    while p < limit and data[p] in (10, 13): p += 1
    if p >= limit: break
    
    # id
    if limit - p >= 3 and data[p:p+3] == b'\xef\xbb\xbf': p += 3
    while p < limit and data[p] != 44: p += 1
    p += 1 # skip ','
    
    # name
    if p < limit and data[p] == 34:
        p += 1
        while p < limit:
            q = data.find(b'"', p)
            if q == -1: p = limit; break
            if q + 1 < limit and data[q+1] == 34:
                p = q + 2
            else:
                p = q + 1
                break
        if p < limit and data[p] == 44: p += 1
    else:
        while p < limit and data[p] != 44: p += 1
        if p < limit and data[p] == 44: p += 1
        
    # note
    if p < limit and data[p] == 34:
        p += 1
        while p < limit:
            q = data.find(b'"', p)
            if q == -1: p = limit; break
            if q + 1 < limit and data[q+1] == 34:
                p = q + 2
            else:
                p = q + 1
                break
        if p < limit and data[p] == 44: p += 1
    else:
        while p < limit and data[p] != 44: p += 1
        if p < limit and data[p] == 44: p += 1
        
    # region_id
    while p < limit and data[p] != 44: p += 1
    if p < limit and data[p] == 44: p += 1
    
    # amount
    start_amount = p
    # Old method
    neg_old = (data[p] == 45)
    if neg_old: p += 1
    val_old = 0
    while p < limit and data[p] not in (10, 13, 34):
        if 48 <= data[p] <= 57:
            val_old = val_old * 10 + (data[p] - 48)
        p += 1
    if neg_old: val_old = -val_old
    while p < limit and data[p] in (10, 13): p += 1
    
    # New fast method
    p2 = start_amount
    neg_new = (data[p2] == 45)
    if neg_new: p2 += 1
    val_new = 0
    while 48 <= data[p2] <= 57:
        val_new = val_new * 10 + (data[p2] - 48)
        p2 += 1
    if data[p2] == 46: p2 += 1 # skip '.'
    val_new = val_new * 100 + (data[p2] - 48) * 10 + (data[p2+1] - 48)
    p2 += 2
    if neg_new: val_new = -val_new
    
    if val_old != val_new:
        mismatches += 1
        print(f'Mismatch: old={val_old}, new={val_new}, raw={data[start_amount:p]}')
        if mismatches > 10: break
    total += 1

print(f'Total records verified: {total}, mismatches: {mismatches}')
assert mismatches == 0
