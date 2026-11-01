import urllib.request
import json
import math

url = 'https://s3.amazonaws.com/raptors.dev/speeddemon.dev/2026/board/board.json'
try:
    req = urllib.request.Request(url, headers={'User-Agent': 'Mozilla/5.0'})
    with urllib.request.urlopen(req, timeout=10) as resp:
        data = json.loads(resp.read().decode())
    teams = data.get('teams', [])
    
    scored_teams = []
    for t in teams:
        slug = t.get('slug')
        name = t.get('team') or slug
        tracks = t.get('tracks', {})
        track_scores = {}
        for tr in ['a', 'b', 'c', 'd']:
            best = tracks.get(tr, {}).get('best')
            if best and best.get('ok') and best.get('points') is not None:
                track_scores[tr] = best.get('points')
            else:
                track_scores[tr] = 0.0
        # Overall score is geometric mean of top 3 scores
        sorted_scores = sorted(track_scores.values(), reverse=True)
        top3 = sorted_scores[:3]
        if len(top3) == 3 and all(s > 0 for s in top3):
            overall = (top3[0] * top3[1] * top3[2]) ** (1.0 / 3.0)
        else:
            overall = 0.0
        scored_teams.append({
            'name': name,
            'slug': slug,
            'overall': overall,
            'scores': track_scores,
            'top3': top3,
            'raw': t
        })
        
    scored_teams.sort(key=lambda x: x['overall'], reverse=True)
    
    print(f"Total teams: {len(scored_teams)}\n")
    print("--- TOP 10 TEAMS ---")
    for idx, t in enumerate(scored_teams[:10]):
        print(f"#{idx+1:2d}: {t['name']:<25} | Overall: {t['overall']:6.2f} pts | A: {t['scores']['a']:5.2f} | B: {t['scores']['b']:5.2f} | C: {t['scores']['c']:5.2f} | D: {t['scores']['d']:5.2f}")
        
    for idx, t in enumerate(scored_teams):
        if t['slug'] == 'cache-me-if-you-can-c1d0b8':
            print(f"\n>>> OUR TEAM <<<")
            print(f"Rank #{idx+1}: {t['name']} ({t['slug']}) | Overall: {t['overall']:6.2f} pts")
            print(f"Track Scores: A={t['scores']['a']:.2f}, B={t['scores']['b']:.2f}, C={t['scores']['c']:.2f}, D={t['scores']['d']:.2f}")
            print(f"Top 3 tracks used: {t['top3']}")
            tracks = t['raw'].get('tracks', {})
            for tr in ['a', 'b', 'c', 'd']:
                best = tracks.get(tr, {}).get('best')
                if best:
                    print(f"  Track {tr.upper()}: T={best.get('T_s')}s, M={best.get('M_bytes')/(1024*1024):.1f}MB, S={best.get('S')}, K={best.get('K'):.2f}, run={best.get('run_id')}")
            break
except Exception as e:
    import traceback
    traceback.print_exc()
