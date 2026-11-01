import urllib.request
import json
import time

url = 'https://s3.amazonaws.com/raptors.dev/speeddemon.dev/2026/board/board.json'

for attempt in range(5):
    try:
        req = urllib.request.Request(url, headers={'User-Agent': 'Mozilla/5.0'})
        with urllib.request.urlopen(req, timeout=10) as resp:
            data = json.loads(resp.read().decode())
        for t in data.get('teams', []):
            if t.get('slug') == 'cache-me-if-you-can-c1d0b8':
                print('Watch:', t.get('watch'))
                print('Queue:', t.get('queue'))
                tracks = t.get('tracks', {})
                for tr in ['a', 'b', 'c', 'd']:
                    lat = tracks.get(tr, {}).get('latest', {})
                    bst = tracks.get(tr, {}).get('best', {})
                    lat_c = lat.get('commit')[:7] if lat.get('commit') else 'none'
                    bst_c = bst.get('commit')[:7] if bst.get('commit') else 'none'
                    print(f"Track {tr.upper()}:")
                    print(f"  Latest: commit={lat_c}, run={lat.get('run_id')}, pts={lat.get('points')}, T={lat.get('T_s')}s, M={lat.get('M_bytes')}, ok={lat.get('ok')}, stage={lat.get('stage')}")
                    print(f"  Best:   commit={bst_c}, run={bst.get('run_id')}, pts={bst.get('points')}, T={bst.get('T_s')}s")
                break
        break
    except Exception as e:
        print(f"Attempt {attempt+1} failed: {e}")
        time.sleep(2)
