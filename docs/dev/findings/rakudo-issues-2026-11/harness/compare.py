# Bucket each snippet run by how the two engines' results differ, joined with issue state.
import json, re, sys
from collections import Counter, defaultdict
res = [json.loads(l) for l in open(sys.argv[1])]
cls = {r['number']: r for r in json.load(open(sys.argv[2]))}
CRASH = re.compile(r'Segmentation fault|Assertion failed|libc\+\+abi|terminate called|internal error|Internal error|stack overflow|Abort trap|bus error|AddressSanitizer|double free|malloc:|panic', re.I)
def norm(s):
    s = re.sub(r'/[^\s:\'"]*/run/\d+-\d+-(pp|rk)/?', 'WORK/', s)
    s = re.sub(r'0x[0-9a-fA-F]{6,}', '0xADDR', s)
    s = re.sub(r'\|\d{5,}', '|ID', s)
    return '\n'.join(l.rstrip() for l in s.rstrip().split('\n'))
def verdict(r):
    pp, rk = r['pp'], r['rk']
    if pp['timeout'] and rk['timeout']: return 'both-timeout'
    if pp['timeout']: return 'pp-timeout'
    if rk['timeout']: return 'rk-timeout'
    ppc = pp['rc'] < 0 or (pp['rc'] != 0 and CRASH.search(pp['err']) is not None)
    rkc = rk['rc'] < 0 or (rk['rc'] != 0 and CRASH.search(rk['err']) is not None)
    if ppc and not rkc: return 'pp-crash'
    if rkc and not ppc: return 'rk-crash'
    if ppc and rkc: return 'both-crash'
    po, ro = norm(pp['out']), norm(rk['out'])
    if pp['rc'] == 0 and rk['rc'] == 0:
        return 'match' if po == ro else 'out-diff'
    if pp['rc'] != 0 and rk['rc'] != 0:
        return 'both-die' if po == ro else 'both-die-out-diff'
    return 'pp-dies' if pp['rc'] != 0 else 'rk-dies'
out = []
for r in res:
    v = verdict(r)
    c = cls.get(r['number'], {})
    r['verdict'] = v; r['state'] = c.get('state'); r['reason'] = c.get('state_reason')
    out.append(r)
with open(sys.argv[3], 'w') as f:
    for r in out:
        f.write(json.dumps(r) + '\n')
by = defaultdict(Counter)
for r in out:
    by[r['state']][r['verdict']] += 1
for st, c in by.items():
    print(st, sum(c.values()), c.most_common())
# Per-issue roll-up: worst verdict across its snippets
rank = ['pp-crash', 'pp-timeout', 'pp-dies', 'out-diff', 'both-die-out-diff', 'rk-dies', 'rk-crash', 'rk-timeout', 'both-crash', 'both-timeout', 'both-die', 'match']
per = defaultdict(list)
for r in out:
    per[r['number']].append(r['verdict'])
roll = {n: min(vs, key=rank.index) for n, vs in per.items()}
by2 = defaultdict(Counter)
for n, v in roll.items():
    by2[cls.get(n, {}).get('state')][v] += 1
print('per-issue:')
for st, c in by2.items():
    print(' ', st, sum(c.values()), c.most_common())
json.dump(roll, open(sys.argv[3] + '.roll.json', 'w'))
