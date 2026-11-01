hours = [
    f"2026-10-30T{h:02d}:00:00Z" for h in range(18, 24)
] + [
    f"2026-10-31T{h:02d}:00:00Z" for h in range(0, 24)
] + [
    f"2026-11-01T{h:02d}:00:00Z" for h in range(0, 24)
] + [
    f"2026-11-02T{h:02d}:00:00Z" for h in range(0, 18)
]

print("Total hours defined:", len(hours))
assert len(hours) == 72
assert hours == sorted(hours)

def get_hour_idx(ts_bytes):
    # ts: 2026-10-30T18:00:00...
    m = ts_bytes[6] # '0' for Oct (10), '1' for Nov (11)
    d = (ts_bytes[8] - 48) * 10 + (ts_bytes[9] - 48)
    h = (ts_bytes[11] - 48) * 10 + (ts_bytes[12] - 48)
    if m == 48: # '0' -> month 10
        if d == 30: return h - 18
        else: return 6 + h
    else: # month 11
        if d == 1: return 30 + h
        else: return 54 + h

with open('data/a/requests.log', 'rb') as f:
    for i, line in enumerate(f):
        idx = get_hour_idx(line)
        expected_str = line[:13].decode() + ":00:00Z"
        if hours[idx] != expected_str:
            print(f"Mismatch at line {i}: {line[:13]} -> idx {idx} -> {hours[idx]}")
            break
    else:
        print("ALL 310,000 LINES MATCH THE EXACT HOUR INDEX PERFECTLY!")
