import urllib.request, json
req = urllib.request.Request('https://s3.amazonaws.com/raptors.dev/speeddemon.dev/2026/board/board.json', headers={'User-Agent': 'Mozilla/5.0'})
with urllib.request.urlopen(req) as resp:
    data = json.loads(resp.read().decode())
for t in data.get('teams', []):
    if t.get('team') in ['Cause Of Death', 'Kirk Mates', 'NickTrapp', 'Cache Me']:
        print(f"=== {t.get('team')} ===")
        for tr in ['a', 'b', 'c', 'd']:
            b = t.get('tracks', {}).get(tr, {}).get('best', {})
            mb = b.get("M_bytes", 0) / (1024*1024) if b.get("M_bytes") else 0
            print(f"  Track {tr.upper()}: score={b.get('points', 0):.2f}, T={b.get('T_s')}s, M={mb:.1f}MB, S={b.get('S')}, lang={b.get('lang')}")
