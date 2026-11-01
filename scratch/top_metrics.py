import urllib.request, json

req = urllib.request.Request('https://s3.amazonaws.com/raptors.dev/speeddemon.dev/2026/board/board.json', headers={'User-Agent': 'Mozilla/5.0'})
data = json.loads(urllib.request.urlopen(req).read().decode())

target_slugs = ['kirk-mates', 'all-star', 'cause-of-death', 'another-ello']
for t in data.get('teams', []):
    slug = t.get('slug', '')
    name = t.get('team', '')
    if any(s in slug.lower() or s in name.lower() for s in ['kirk', 'all star', 'cause of death', 'another ello']):
        print(f"=== {name} ({slug}) ===")
        for trk in ['a', 'b', 'c', 'd']:
            b = t.get('tracks', {}).get(trk, {}).get('best', {})
            pts = b.get('points') or 0
            ts = b.get('T_s') or 0
            mb = (b.get('M_bytes') or 0) / 1e6
            print(f"   {trk.upper()}: pts={pts:.2f} | T={ts:.4f}s | M={mb:.1f} MB")
