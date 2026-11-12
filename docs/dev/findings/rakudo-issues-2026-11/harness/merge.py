# Merge classifier TSVs, harness verdicts and review verdicts into one table per issue.
import json, sys, glob, re
from collections import Counter
A = set('regex grammar strings unicode numeric lists hashes oo dispatch concurrency io datetime exceptions syntax control scope modules nativecall pod repl cli eval formatting rakuast build backend test perf misc'.split())
K = set('bug crash hang lta perf feature design regression build docs'.split()); R = set('lang error tool design impl'.split())
FIX = {'phasers': 'control', 'docs': 'misc', 'operators': 'syntax', 'types': 'oo', 'signatures': 'dispatch', 'async': 'concurrency', 'containers': 'scope', 'precomp': 'modules', 'jvm': 'backend', 'moarvm': 'backend'}
iss = {i['number']: i for i in json.load(open('raw/issues.json'))}
cls = {}
for f in sorted(glob.glob('work/class/part*.tsv')):
    for l in open(f):
        p = l.rstrip('\n').split('\t')
        if len(p) != 6 or not p[0].isdigit(): continue
        n, a, a2, k, r, g = p
        a = FIX.get(a, a); a2 = FIX.get(a2, a2)
        if a not in A: a = 'misc'
        if a2 not in A or a2 == a: a2 = '-'
        if k not in K: k = 'bug'
        if r not in R: r = 'lang'
        cls[int(n)] = (a, a2, k, r, g.strip())
roll = json.load(open('work/verdicts.jsonl.roll.json'))
rev = {}
for f in sorted(glob.glob('work/review/out*.tsv')):
    for l in open(f):
        p = l.rstrip('\n').split('\t')
        if len(p) >= 4 and re.match(r'^\d+\.\d+$', p[0]):
            rev[p[0]] = (p[1].strip(), p[2].strip(), p[3].strip())
missing = [n for n in iss if n not in cls]
print('classified', len(cls), 'missing', len(missing), missing[:20])
rows = []
for n in sorted(iss):
    it = iss[n]
    a, a2, k, r, g = cls.get(n, ('misc', '-', 'bug', 'lang', ''))
    revs = sorted((cid, v) for cid, v in rev.items() if cid.split('.')[0] == str(n))
    rows.append({'number': n, 'state': it['state'] if it['state'] == 'open' else ('closed' if it.get('state_reason') != 'not_planned' else 'wontfix'),
                 'created': it['created_at'][:10], 'closed': (it.get('closed_at') or '')[:10],
                 'area': a, 'area2': a2, 'kind': k, 'rel': r, 'gist': g, 'title': it['title'],
                 'labels': [l['name'] for l in it['labels']], 'harness': roll.get(str(n), ''),
                 'review': [(cid, v[0], v[1], v[2]) for cid, v in revs]})
json.dump(rows, open('work/merged.json', 'w'))
print(Counter(r['rel'] for r in rows))
