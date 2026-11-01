import hashlib

with open('data/a/summary.txt', 'rb') as f:
    expected = f.read()
expected_sha = hashlib.sha256(expected).hexdigest()
print('Target SHA256:', expected_sha)

# Read file in 512KB chunks and parse lines
from collections import defaultdict

hours_data = defaultdict(lambda: defaultdict(list))
hours_errors = defaultdict(lambda: defaultdict(int))

with open('data/a/requests.log', 'rb') as f:
    leftover = b''
    while True:
        chunk = f.read(524288)
        if not chunk and not leftover:
            break
        buf = leftover + chunk
        last_nl = buf.rfind(b'\n')
        if last_nl == -1:
            leftover = buf
            continue
        to_parse = buf[:last_nl+1]
        leftover = buf[last_nl+1:]
        
        for line in to_parse.splitlines():
            if not line: continue
            parts = line.split()
            hr = parts[0][:13].decode('ascii')
            ep = parts[2].decode('ascii')
            st = parts[3].decode('ascii')
            lat = int(parts[4])
            is_err = 1 if st.startswith('5') else 0
            hours_data[hr][ep].append(lat)
            if is_err:
                hours_errors[hr][ep] += 1

out_lines = []
for hr in sorted(hours_data.keys()):
    hr_str = hr + ":00:00Z"
    eps = hours_data[hr]
    for ep in sorted(eps.keys()):
        lat = eps[ep]
        n = len(lat)
        lat.sort()
        idx50 = (50 * n + 99) // 100 - 1
        idx95 = (95 * n + 99) // 100 - 1
        idx99 = (99 * n + 99) // 100 - 1
        p50 = lat[idx50]
        p95 = lat[idx95]
        p99 = lat[idx99]
        errs = hours_errors[hr][ep]
        ppm = errs * 1000000 // n
        out_lines.append(f"{hr_str} {ep} {n} {p50} {p95} {p99} {errs} {ppm}\n")

gen = "".join(out_lines).encode('ascii')
gen_sha = hashlib.sha256(gen).hexdigest()
print('Generated SHA256:', gen_sha)
assert gen_sha == expected_sha
print('STREAMING VERIFICATION SUCCESS!')
