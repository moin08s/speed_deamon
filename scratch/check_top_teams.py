import urllib.request
import json

req = urllib.request.Request('https://s3.amazonaws.com/raptors.dev/speeddemon.dev/2026/board/board.json', headers={'User-Agent': 'Mozilla/5.0'})
with urllib.request.urlopen(req) as resp:
    data = json.loads(resp.read().decode())

top_teams = ['Cause Of Death', 'NickTrapp', 'Kirk Mates', 'Cache Me If You Can']
for t in data.get('teams', []):
    if t.get('team') in top_teams:
        print('=== ' + t.get('team') + ' ===')
        for tr in ['a', 'b', 'c', 'd']:
            run = t.get('tracks', {}).get(tr, {}).get('best', {})
            if run:
                pts = run.get('points', 0)
                ts = run.get('T_s', 0)
                mb = run.get('M_bytes', 0) / (1024 * 1024)
                s = run.get('S', 0)
                d = run.get('D', 0)
                k = run.get('K', 0)
                lang = run.get('lang', [])
                print(f"  Track {tr.upper()}: pts={pts:.2f}, T={ts:.5f}s, M={mb:.1f}MB, S={s}, D={d}, K={k:.2f}, lang={lang}")
