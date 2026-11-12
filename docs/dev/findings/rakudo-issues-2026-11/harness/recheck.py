# Re-run named cases (issue.k) on both engines in the sandbox; print short results.
import json, sys, os, re
here = os.path.dirname(os.path.abspath(__file__))
src = open(os.path.join(here, 'runall.py')).read()
ns = {'__file__': os.path.join(here, 'runall.py')}
exec(src.split('src, dst = sys.argv')[0] + "\nengines=['pp','rk']\n" + re.search(r'^def run1.*?(?=^def job)', src, re.M | re.S).group(0), ns)
snips = {json.loads(l)['number']: json.loads(l)['snips'] for l in open('work/snips.jsonl')}
for cid in sys.argv[1:]:
    n, k = map(int, cid.split('.'))
    s = snips[n][k]
    print(f'== {cid}')
    for eng in ('pp', 'rk'):
        wd = os.path.join(ns['RUNROOT'], f'chk-{n}-{k}-{eng}')
        ns['shutil'].rmtree(wd, ignore_errors=True); os.makedirs(wd)
        open(os.path.join(wd, 'snippet.raku'), 'w').write(s['code'] + '\n')
        r = ns['run1'](eng, wd, s)
        ns['shutil'].rmtree(wd, ignore_errors=True)
        o = (r['out'][-300:] + ' || ' + r['err'][:200]).replace('\n', '⏎')
        print(f"  {eng} rc={'TO' if r['timeout'] else r['rc']}: {o}")
