import json

with open('scratch/board.json', encoding='utf-8') as f:
    d = json.load(f)

# Sort teams by their total or average score
def get_avg(t):
    pts = []
    for trk in ['a', 'b', 'c', 'd']:
        best = t.get('tracks', {}).get(trk, {}).get('best', {})
        if best and best.get('points') is not None:
            pts.append(best['points'])
        else:
            pts.append(0.0)
    return sum(pts) / 4.0

sorted_teams = sorted(d['teams'], key=get_avg, reverse=True)

print("=" * 110)
print(f"{'Rank':4} | {'Team':28} | {'Avg':6} | {'Track A (T/M/Pts)':20} | {'Track B (T/M/Pts)':20} | {'Track C (T/M/Pts)':20} | {'Track D (T/M/Pts)':20}")
print("=" * 110)

for rank, t in enumerate(sorted_teams[:10], 1):
    avg = get_avg(t)
    row = [f"{rank:4}", f"{t['team'][:28]:28}", f"{avg:6.2f}"]
    for trk in ['a', 'b', 'c', 'd']:
        b = t.get('tracks', {}).get(trk, {}).get('best', {})
        if b and b.get('points') is not None:
            T = b.get('T_s', 0)
            M = b.get('M_bytes', 0) / 1e6
            pts = b.get('points', 0)
            row.append(f"{T:.3f}s/{M:4.1f}M/{pts:5.2f}")
        else:
            row.append("  -  ")
    print(" | ".join(row))

print("=" * 110)

our = next((t for t in d['teams'] if t['slug'] == 'cache-me-if-you-can-c1d0b8'), None)
if our:
    rank = next((i for i, t in enumerate(sorted_teams, 1) if t['slug'] == 'cache-me-if-you-can-c1d0b8'), 999)
    avg = get_avg(our)
    row = [f"{rank:4}", f"{our['team'][:28]:28}", f"{avg:6.2f}"]
    for trk in ['a', 'b', 'c', 'd']:
        b = our.get('tracks', {}).get(trk, {}).get('best', {})
        if b and b.get('points') is not None:
            T = b.get('T_s', 0)
            M = b.get('M_bytes', 0) / 1e6
            pts = b.get('points', 0)
            row.append(f"{T:.3f}s/{M:4.1f}M/{pts:5.2f}")
        else:
            row.append("  -  ")
    print(" | ".join(row))
