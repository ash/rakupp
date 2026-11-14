# Emit the committed data files (issues.tsv, review.tsv) and markdown fragments for the doc.
import json, sys, re
from collections import Counter, defaultdict
OUT = sys.argv[1]
rows = json.load(open('work/merged.json'))
idx = {c['case']: c for c in json.load(open('work/review/index.json'))}
iss = {i['number']: i for i in json.load(open('raw/issues.json'))}
def clean(s): return re.sub(r'[\t\n]+', ' ', s).strip()
with open(f'{OUT}/issues.tsv', 'w') as f:
    f.write('number\tstate\topened\tclosed\tarea\tarea2\tkind\trel\tharness\treview\tlabels\ttitle\tgist\n')
    for r in rows:
        rv = ','.join(sorted(set(v for _, v, _, _ in r['review'])))
        f.write('\t'.join(map(str, [r['number'], r['state'], r['created'], r['closed'], r['area'], r['area2'], r['kind'], r['rel'],
                                     r['harness'] or '-', rv or '-', ','.join(r['labels']) or '-', clean(r['title']), clean(r['gist'])])) + '\n')
rev = []
for r in rows:
    for cid, v, a, note in r['review']:
        c = idx.get(cid, {})
        rev.append((r['number'], int(cid.split('.')[1]), cid, r['state'], c.get('verdict', ''), 'yes' if c.get('nondet') else '', v, a, clean(note), clean(r['title'])))
with open(f'{OUT}/review.tsv', 'w') as f:
    f.write('case\tissue_state\tharness\tvaries\tverdict\tarea\tnote\ttitle\n')
    for n, k, cid, st, h, nd, v, a, note, t in sorted(rev):
        f.write('\t'.join([cid, st, h, nd, v, a, note, t]) + '\n')
print('review rows', len(rev), Counter(x[6] for x in rev).most_common())
