# Run each extracted snippet under Raku++ and Rakudo inside a write/network sandbox.
# Usage: runall.py snippets.jsonl results.jsonl [max-per-issue] [workers]
import json, os, sys, subprocess, signal, time, shutil, re
from concurrent.futures import ThreadPoolExecutor
SP = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SB = os.path.join(SP, 'scripts', 'sb.sb')
RUNROOT = os.path.join(SP, 'run')
ENGINES = {'pp': '/Users/ash/rakupp/build/rakupp',
           'rk': '/Users/ash/.rakubrew/versions/moar-2026.09/install/bin/rakudo'}
TIMEOUT = float(os.environ.get('SNIP_TIMEOUT', '10'))
src, dst = sys.argv[1], sys.argv[2]
maxper = int(sys.argv[3]) if len(sys.argv) > 3 else 3
workers = int(sys.argv[4]) if len(sys.argv) > 4 else 8
engines = os.environ.get('ENGINES', 'pp,rk').split(',')
done = set()
if os.path.exists(dst):
    for l in open(dst):
        try:
            r = json.loads(l); done.add((r['number'], r['k']))
        except Exception:
            pass
jobs = []
for l in open(src):
    o = json.loads(l)
    for k, s in enumerate(o['snips'][:maxper]):
        if (o['number'], k) not in done:
            jobs.append((o['number'], k, s))
def run1(eng, wd, s):
    flags = [f for f in s.get('flags', []) if re.match(r'^-(n|p|M\w[\w:]*|I\S+)$', f)]
    if s['src'] in ('cmd', 'bot', 'bot-inline', 'cmd-inline'):
        argv = [ENGINES[eng]] + flags + ['-e', s['code']]
    else:
        argv = [ENGINES[eng]] + flags + ['snippet.raku']
    env = {'HOME': wd, 'PATH': '/usr/bin:/bin', 'TMPDIR': wd, 'LANG': 'en_US.UTF-8', 'RAKUDO_ERROR_COLOR': '0', 'NO_COLOR': '1', 'TERM': 'dumb'}
    t0 = time.time()
    p = subprocess.Popen(['/usr/bin/sandbox-exec', '-f', SB, '-D', 'WORK=' + wd] + argv, cwd=wd, env=env,
                         stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.PIPE, start_new_session=True)
    to = False
    try:
        out, err = p.communicate(timeout=TIMEOUT)
    except subprocess.TimeoutExpired:
        to = True
        try:
            os.killpg(p.pid, signal.SIGKILL)
        except Exception:
            pass
        try:
            out, err = p.communicate(timeout=5)
        except Exception:
            out, err = b'', b''
    dt = time.time() - t0
    rc = p.returncode
    return {'rc': rc, 'timeout': to, 'secs': round(dt, 2),
            'out': out[:6000].decode('utf-8', 'replace'), 'err': err[:3000].decode('utf-8', 'replace')}
def job(j):
    num, k, s = j
    res = {'number': num, 'k': k, 'src': s['src']}
    for eng in engines:
        wd = os.path.join(RUNROOT, f'{num}-{k}-{eng}')
        shutil.rmtree(wd, ignore_errors=True); os.makedirs(wd)
        with open(os.path.join(wd, 'snippet.raku'), 'w') as f:
            f.write(s['code'] + '\n')
        res[eng] = run1(eng, wd, s)
        shutil.rmtree(wd, ignore_errors=True)
    return res
with open(dst, 'a') as f, ThreadPoolExecutor(workers) as ex:
    for i, r in enumerate(ex.map(job, jobs)):
        f.write(json.dumps(r) + '\n'); f.flush()
        if i % 100 == 0:
            print(f'{i}/{len(jobs)}', file=sys.stderr, flush=True)
print('done', len(jobs))
