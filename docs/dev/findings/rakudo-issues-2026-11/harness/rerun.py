# Re-run disagreeing snippets once more on both engines to flag nondeterministic output.
import json, sys, os, re
from concurrent.futures import ThreadPoolExecutor
here = os.path.dirname(os.path.abspath(__file__))
src = open(os.path.join(here, 'runall.py')).read()
ns = {'__file__': os.path.join(here, 'runall.py')}
header = src.split('src, dst = sys.argv')[0]
run1_src = re.search(r'^def run1.*?(?=^def job)', src, re.M | re.S).group(0)
exec(header + "\nengines = ['pp', 'rk']\n" + run1_src, ns)
verd = [json.loads(l) for l in open(sys.argv[1])]
snips = {json.loads(l)['number']: json.loads(l)['snips'] for l in open(sys.argv[2])}
SKIP = {'match', 'both-die', 'both-timeout', 'both-crash'}
todo = [v for v in verd if v['verdict'] not in SKIP]
def job(v):
    s = snips[v['number']][v['k']]
    out = {'number': v['number'], 'k': v['k']}
    for eng in ('pp', 'rk'):
        wd = os.path.join(ns['RUNROOT'], f"re-{v['number']}-{v['k']}-{eng}")
        ns['shutil'].rmtree(wd, ignore_errors=True); os.makedirs(wd)
        open(os.path.join(wd, 'snippet.raku'), 'w').write(s['code'] + '\n')
        r = ns['run1'](eng, wd, s)
        ns['shutil'].rmtree(wd, ignore_errors=True)
        out[eng] = r
    return out
with open(sys.argv[3], 'w') as f, ThreadPoolExecutor(8) as ex:
    for r in ex.map(job, todo):
        f.write(json.dumps(r) + '\n'); f.flush()
print('reran', len(todo))
