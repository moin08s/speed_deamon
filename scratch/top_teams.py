import urllib.request, json

req = urllib.request.Request('https://s3.amazonaws.com/raptors.dev/speeddemon.dev/2026/board/board.json', headers={'User-Agent': 'Mozilla/5.0'})
data = json.loads(urllib.request.urlopen(req).read().decode())

def get_team_score(t):
    # calculate total score across tracks
    trks = t.get('tracks', {})
    pts = []
    for trk in ['a', 'b', 'c', 'd']:
        b = trks.get(trk, {}).get('best')
        if b and b.get('points'):
            pts.append(b['points'])
        else:
            pts.append(0.0)
    # geometric or arithmetic mean?
    return sum(pts) / 4.0, pts

scored_teams = []
for t in data.get('teams', []):
    avg, pts = get_team_score(t)
    name = t.get('team') or t.get('slug')
    scored_teams.append((avg, name, pts, t))

scored_teams.sort(key=lambda x: x[0], reverse=True)

print("TOP 10 TEAMS ON LEADERBOARD:")
for rank, (avg, name, pts, t) in enumerate(scored_teams[:10], 1):
    print(f"#{rank}: {name:<25} Avg={avg:.2f} | A={pts[0]:.2f} B={pts[1]:.2f} C={pts[2]:.2f} D={pts[3]:.2f}")

print("\nOUR TEAM:")
for rank, (avg, name, pts, t) in enumerate(scored_teams, 1):
    if 'cache-me-if-you-can' in str(t.get('slug')):
        print(f"#{rank}: {name:<25} Avg={avg:.2f} | A={pts[0]:.2f} B={pts[1]:.2f} C={pts[2]:.2f} D={pts[3]:.2f}")
        for trk in ['a', 'b', 'c', 'd']:
            b = t.get('tracks', {}).get(trk, {}).get('best', {})
            print(f"   {trk.upper()}: pts={b.get('points',0):.2f} T={b.get('T_s',0):.4f}s M={b.get('M_bytes',0)/1e6:.1f}MB")
        break
