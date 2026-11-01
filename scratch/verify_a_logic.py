import hashlib

# Let's verify whether our algorithm matches data/a/summary.txt exactly
with open('data/a/summary.txt', 'rb') as f:
    expected_hash = hashlib.sha256(f.read()).hexdigest()
print('Expected hash:', expected_hash)

# Now parse data/a/requests.log using the exact same logic
from collections import defaultdict

hours_data = defaultdict(lambda: defaultdict(list))
hours_errors = defaultdict(lambda: defaultdict(int))

with open('data/a/requests.log', 'r') as f:
    for line in f:
        line = line.strip()
        if not line: continue
        parts = line.split()
        hr = parts[0][:13]
        ep = parts[2]
        st = parts[3]
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

generated = "".join(out_lines).encode('utf-8')
gen_hash = hashlib.sha256(generated).hexdigest()
print('Generated hash:', gen_hash)
assert gen_hash == expected_hash, f"Mismatch! {gen_hash} != {expected_hash}"
print('PERFECT MATCH CONFIRMED!')
