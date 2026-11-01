import urllib.request, json, time, sys, subprocess

commit = sys.argv[1] if len(sys.argv) > 1 else None
if not commit:
    commit = subprocess.check_output(['git', 'rev-parse', '--short', 'HEAD']).decode().strip()

url = 'https://s3.amazonaws.com/raptors.dev/speeddemon.dev/2026/board/board.json'
print(f'Watching for commit {commit}...')

for i in range(120):
    time.sleep(5)
    try:
        req = urllib.request.Request(url, headers={'User-Agent': 'Mozilla/5.0'})
        data = json.loads(urllib.request.urlopen(req, timeout=10).read())
        t = next(x for x in data['teams'] if x['slug'] == 'cache-me-if-you-can-c1d0b8')
        infl = t.get('queue', {}).get('in_flight')
        q = t.get('queue', {}).get('queued')
        st = infl.get('state') if infl else None
        trk = infl.get('track') if infl else None
        
        ta = t.get('tracks', {}).get('a', {}).get('latest', {})
        tb = t.get('tracks', {}).get('b', {}).get('latest', {})
        tc = t.get('tracks', {}).get('c', {}).get('latest', {})
        td = t.get('tracks', {}).get('d', {}).get('latest', {})
        
        ca = ta.get('commit', '')[:7]
        cb = tb.get('commit', '')[:7]
        cc = tc.get('commit', '')[:7]
        cd = td.get('commit', '')[:7]
        
        print(f'[{i+1}] queued={q is not None} in_flight={st} (trk={trk}) | A={ca} B={cb} C={cc} D={cd}', flush=True)
        
        done = []
        for name, tr in [('A', ta), ('B', tb), ('C', tc), ('D', td)]:
            c = tr.get('commit', '')[:7]
            if c.startswith(commit):
                stage = tr.get('stage')
                pts = tr.get('points')
                Ts = tr.get('T_s')
                Mb = tr.get('M_bytes', 0) / 1e6
                run = tr.get('run_id')
                fail = tr.get('failure')
                print(f"  Track {name}: stage={stage} pts={pts} T={Ts}s M={Mb:.1f}MB run={run} fail={fail}", flush=True)
                if stage in ['scored', 'failed']:
                    done.append(name)
        if len(done) == 4:
            print("All 4 tracks finished for commit", commit)
            break
    except Exception as e:
        print('Error:', e, flush=True)
