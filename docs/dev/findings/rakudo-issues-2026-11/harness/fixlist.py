# Markdown lists of review verdicts, grouped by verdict then area; marks hand-verified issues.
import csv, sys, re
from collections import defaultdict
V = set(map(int, '''1285 1355 1629 1855 1286 1314 1828 1427 1447 1551 1243 1460 1692 1513 3844 5144 4351 2000 6128 1616 4757
6031 2217 4752 5908 6113 1961 6860 2068 4665 4666 5835 5799 4128 2243 1974 1264 2485 1360 5901 5889 5951 3376 5138 5868
5537 3775 2097 6810 2078 2434 3881 4553 2059 2663 5027 2129 3410 1399 1398 2174 3292 2791 3976 3621 4543 2165 4463
2336 2844 1416 1959 2275 6048 4175 3802 2753 4967 1778 4780 6873 4853 3686 2783'''.split()))
rows = list(csv.DictReader(open(sys.argv[1]), delimiter='\t'))
want = sys.argv[2].split(',')
URL = 'https://github.com/rakudo/rakudo/issues/'
by = defaultdict(lambda: defaultdict(list))
phaser = []
for r in rows:
    if r['verdict'] not in want: continue
    n = int(r['case'].split('.')[0])
    if 'KNOWN-PHASER-VALUE' in r['note']:
        phaser.append(n); continue
    by[r['verdict']][r['area']].append((n, r))
for v in want:
    if not by[v]: continue
    tot = sum(len(x) for x in by[v].values())
    print(f'\n### {v} ({tot})\n')
    for area in sorted(by[v], key=lambda a: (-len(by[v][a]), a)):
        items = sorted(by[v][area], key=lambda x: (x[0], x[1]['case']))
        print(f'**{area}** ({len(items)})\n')
        seen = {}
        for n, r in items:
            note = r['note'].strip()
            key = (n, note)
            if key in seen: continue
            seen[key] = 1
            mark = ' ✓' if n in V else ''
            st = '' if r['issue_state'] == 'closed' else ' *(open)*'
            print(f"- [#{n}]({URL}{n}){st}{mark} — {note}")
        print()
if phaser:
    ps = sorted(set(phaser))
    print(f'\n(KNOWN-PHASER-VALUE, {len(phaser)} cases in {len(ps)} issues: ' + ', '.join(f'[#{n}]({URL}{n})' for n in ps) + ')')
