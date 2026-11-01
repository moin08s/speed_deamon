import urllib.request, json

url = 'https://s3.amazonaws.com/raptors.dev/speeddemon.dev/2026/board/board.json'
req = urllib.request.Request(url, headers={'User-Agent': 'Mozilla/5.0'})
data = json.loads(urllib.request.urlopen(req, timeout=10).read().decode())

for i, t in enumerate(data['teams']):
    name = t.get('name') or t.get('team') or ''
    slug = t.get('slug', '')
    if 'cache' in name.lower() or 'cache' in slug.lower():
        print(f"Team: {name} (slug: {slug}) | Global Rank: #{t.get('rank', i+1)} | Overall: {t.get('score')}")
        for tr in ['a', 'b', 'c', 'd']:
            b = t.get('tracks', {}).get(tr, {}).get('best', {})
            l = t.get('tracks', {}).get(tr, {}).get('latest', {})
            print(f"  Track {tr.upper()}:")
            print(f"    BEST:   pts={b.get('points')} T={b.get('T_s')}s M={b.get('M_bytes', 0)/1e6:.1f}MB run={b.get('run_id')}")
            print(f"    LATEST: pts={l.get('points')} T={l.get('T_s')}s M={l.get('M_bytes', 0)/1e6:.1f}MB run={l.get('run_id')}")
