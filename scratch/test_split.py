import re

with open('data/c/records.csv', 'rb') as f:
    data = f.read()

size = len(data)
num_threads = 12

# Method 1: The slow 20MB scan
def get_slow_splits():
    running_quotes = 0
    scan_idx = 0
    splits = [0]
    for i in range(1, num_threads):
        target = i * (size // num_threads)
        while scan_idx < target and scan_idx < size:
            if data[scan_idx] == 34: # '"'
                running_quotes += 1
            scan_idx += 1
        while scan_idx < size:
            if data[scan_idx] == 34:
                running_quotes += 1
            if data[scan_idx] == 10 and (running_quotes % 2 == 0):
                splits.append(scan_idx + 1)
                scan_idx += 1
                break
            scan_idx += 1
    splits.append(size)
    return splits

slow_splits = get_slow_splits()
print('Slow splits:', slow_splits)

# Method 2: Fast localized split finder
def get_fast_split(target):
    # Find candidate newlines near target
    idx = target
    while idx < size:
        # Find next newline
        nl = data.find(b'\n', idx)
        if nl == -1: return size
        
        # Check if preceded by .\d\d
        if nl >= 4 and data[nl-3] == 46 and data[nl-2] in b'0123456789' and data[nl-1] in b'0123456789':
            # Check previous newline to get the line boundaries
            prev_nl = data.rfind(b'\n', max(0, nl - 1000), nl)
            line_start = prev_nl + 1 if prev_nl != -1 else 0
            
            # Check if line starts with digits followed by comma
            p = line_start
            while p < nl and data[p] in b'0123456789':
                p += 1
            if p > line_start and p < nl and data[p] == 44: # ','
                # Count quotes on this single line
                line_quotes = data[line_start:nl].count(b'"')
                if line_quotes % 2 == 0:
                    return nl + 1
        idx = nl + 1
    return size

fast_splits = [0]
for i in range(1, num_threads):
    target = i * (size // num_threads)
    fast_splits.append(get_fast_split(target))
fast_splits.append(size)

print('Fast splits:', fast_splits)
for i in range(len(slow_splits)):
    print(f"Split {i}: slow={slow_splits[i]}, fast={fast_splits[i]}, diff={fast_splits[i] - slow_splits[i]}")
    # Verify that fast split is a true line start
    fs = fast_splits[i]
    if fs < size:
        print(f"  Line at fast split {i}: {data[fs:fs+40]}")
