import json
with open('scratch/board.json', encoding='utf-8') as f:
    d = json.load(f)

print('=== Top 10 Teams Track B ===')
for t in d['teams'][:10]:
    b = t.get('tracks', {}).get('b', {})
    pb = b.get('pb', {})
    print(f"{t['team']:25} | PB pts: {pb.get('points')} | T: {pb.get('T_s')}s | M: {pb.get('M_bytes',0)/1e6:.1f}MB | run: {pb.get('run_id')}")

print('\n=== Our Team Track B ===')
our = next(t for t in d['teams'] if t['slug'] == 'cache-me-if-you-can-c1d0b8')
b = our.get('tracks', {}).get('b', {})
pb = b.get('pb', {})
lat = b.get('latest', {})
print(f"PB:  pts: {pb.get('points')} | T: {pb.get('T_s')}s | M: {pb.get('M_bytes',0)/1e6:.1f}MB | run: {pb.get('run_id')}")
print(f"Lat: pts: {lat.get('points')} | T: {lat.get('T_s')}s | M: {lat.get('M_bytes',0)/1e6:.1f}MB | run: {lat.get('run_id')}")
