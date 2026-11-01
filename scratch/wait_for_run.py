import urllib.request, json, time, sys, subprocess

URL = 'https://s3.amazonaws.com/raptors.dev/speeddemon.dev/2026/board/board.json'

def get_team():
    req = urllib.request.Request(URL, headers={'User-Agent': 'Mozilla/5.0'})
    with urllib.request.urlopen(req, timeout=10) as resp:
        data = json.loads(resp.read().decode())
    for t in data.get('teams', []):
        if 'c1d0b8' in t.get('slug', ''):
            return t
    return None

if len(sys.argv) > 1:
    target_commit = sys.argv[1][:7]
else:
    target_commit = subprocess.check_output(['git', 'rev-parse', '--short', 'HEAD']).decode().strip()[:7]
print(f"Waiting for commit {target_commit} to be picked up and scored...")

for attempt in range(30):
    try:
        team = get_team()
        if team:
            head = team.get('watch', {}).get('head', '')[:7]
            q = team.get('queue', {})
            in_fl = q.get('in_flight')
            tracks = team.get('tracks', {})
            all_done = True
            summary = []
            for tr in ['a', 'b', 'c', 'd']:
                latest = tracks.get(tr, {}).get('latest', {})
                c = latest.get('commit', '')[:7]
                st = latest.get('stage')
                pts = latest.get('points')
                ts = latest.get('T_s')
                mb = (latest.get('M_bytes') or 0) / (1024 * 1024)
                summary.append(f"{tr.upper()}:[{c},{st},{pts:.2f}pts,{ts}s]" if pts else f"{tr.upper()}:[{c},{st}]")
                if c != target_commit or st != 'scored':
                    all_done = False
            print(f"[{time.strftime('%H:%M:%S')}] in_flight={bool(in_fl)} | {' | '.join(summary)}")
            if all_done:
                print("All tracks scored successfully!")
                break
    except Exception as e:
        print("Error:", e)
    time.sleep(15)
