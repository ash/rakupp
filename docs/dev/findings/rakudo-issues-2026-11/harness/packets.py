# Build review packets for disagreements (and open-issue agreements) after the rerun.
import json, re, sys
from collections import Counter
verd = [json.loads(l) for l in open('work/verdicts.jsonl')]
rer = {(r['number'], r['k']): r for r in (json.loads(l) for l in open('work/rerun.jsonl'))}
iss = {i['number']: i for i in json.load(open('raw/issues.json'))}
snips = {json.loads(l)['number']: json.loads(l)['snips'] for l in open('work/snips.jsonl')}
def norm(s):
    s = re.sub(r'/[^\s:\'"]*/run/(re-)?\d+-\d+-(pp|rk)/?', 'WORK/', s)
    s = re.sub(r'0x[0-9a-fA-F]{6,}', '0xADDR', s)
    s = re.sub(r'\|\d{5,}', '|ID', s)
    return '\n'.join(l.rstrip() for l in s.rstrip().split('\n'))
NOISY_DIE = re.compile(r'Two terms in a row|Confused|Could not find \S+ in:|Bogus (term|statement)|Malformed postfix|Preceding context expects a term|Leading 0 has no meaning|Colons may not be used')
def trim(s, n):
    s = s.strip('\n')
    return s if len(s) <= n else s[:n] + f'\n…[{len(s) - n} more chars]'
def excerpt(body, n=1300):
    body = (body or '').replace('\r\n', '\n')
    body = re.sub(r'<!--.*?-->', '', body, flags=re.S)
    body = re.sub(r'(```+)([^\n]*)\n(.*?)\1', lambda m: m.group(1) + m.group(2) + '\n' + trim(m.group(3), 500) + '\n' + m.group(1), body, flags=re.S)
    body = re.sub(r'\n\s*\n+', '\n', body).strip()
    return trim(body, n)
cases = []
for v in verd:
    st = v['state']; vd = v['verdict']
    take = vd not in ('match', 'both-die', 'both-timeout', 'both-crash')
    if st == 'open' and vd == 'match':
        take = True
    if st == 'open' and vd == 'both-die' and not NOISY_DIE.search(v['rk']['err']):
        take = True
    if not take:
        continue
    r2 = rer.get((v['number'], v['k']))
    nd = []
    if r2:
        if norm(r2['rk']['out']) != norm(v['rk']['out']) or (r2['rk']['rc'] == 0) != (v['rk']['rc'] == 0): nd.append('Rakudo output varies between runs')
        if norm(r2['pp']['out']) != norm(v['pp']['out']) or (r2['pp']['rc'] == 0) != (v['pp']['rc'] == 0): nd.append('Raku++ output varies between runs')
    v['nondet'] = nd
    cases.append(v)
print(len(cases), Counter((c['state'], c['verdict']) for c in cases).most_common())
print('nondet', sum(1 for c in cases if c['nondet']))
def eng(r, name):
    rc = 'TIMEOUT(10s)' if r['timeout'] else str(r['rc'])
    return f"{name}: exit={rc}\n-- stdout --\n{trim(norm(r['out']), 700) or '(empty)'}\n-- stderr --\n{trim(norm(r['err']), 450) or '(empty)'}"
nparts = int(sys.argv[1]) if len(sys.argv) > 1 else 8
cases.sort(key=lambda c: (c['number'], c['k']))
# keep one issue's cases in one part
groups = {}
for c in cases:
    groups.setdefault(c['number'], []).append(c)
issue_order = sorted(groups)
parts = [[] for _ in range(nparts)]
sizes = [0] * nparts
for n in issue_order:
    i = sizes.index(min(sizes)); parts[i].extend(groups[n]); sizes[i] += len(groups[n])
index = []
for pi, part in enumerate(parts):
    with open(f'work/review/in{pi+1}.txt', 'w') as f:
        for c in sorted(part, key=lambda c: (c['number'], c['k'])):
            it = iss[c['number']]
            s = snips[c['number']][c['k']]
            cid = f"{c['number']}.{c['k']}"
            index.append({'case': cid, 'part': pi + 1, 'number': c['number'], 'k': c['k'], 'verdict': c['verdict'], 'state': c['state'], 'nondet': c['nondet']})
            st = it['state'] + ('/' + it['state_reason'] if it['state'] == 'closed' and it.get('state_reason') else '')
            labels = ','.join(l['name'] for l in it['labels']) or '-'
            f.write(f"=== CASE {cid}  issue #{c['number']} [{st}, opened {it['created_at'][:10]}{', closed ' + it['closed_at'][:10] if it.get('closed_at') else ''}] labels: {labels}\n")
            f.write(f"TITLE: {it['title']}\n--- ISSUE EXCERPT ---\n{excerpt(it.get('body'))}\n")
            how = '-e' if s['src'] in ('cmd', 'bot', 'bot-inline', 'cmd-inline') else 'file'
            f.write(f"--- SNIPPET (extracted from the issue, run as {how}) ---\n{trim(s['code'], 1500)}\n")
            f.write(f"--- RESULTS (harness bucket: {c['verdict']}) ---\n{eng(c['rk'], 'RAKUDO 2026.09')}\n{eng(c['pp'], 'RAKU++')}\n")
            if c['nondet']:
                f.write('NOTE: ' + '; '.join(c['nondet']) + '\n')
            f.write('\n')
    print(pi + 1, len(part))
json.dump(index, open('work/review/index.json', 'w'))
