# Read-only download of rakudo/rakudo issues (no PRs) via the public search API.
# Partitions by creation date so each query stays under the 1000-result cap.
import json, sys, time, urllib.parse, datetime, subprocess
OUT = sys.argv[1]
API = 'https://api.github.com/search/issues'
CRLF = chr(13) + chr(10)
def get(q, page=1, per=100):
    url = API + '?' + urllib.parse.urlencode({'q': q, 'per_page': per, 'page': page, 'sort': 'created', 'order': 'asc'})
    while True:
        r = subprocess.run(['curl', '-s', '-D', '-', '-H', 'Accept: application/vnd.github+json', '-A', 'issue-survey-script', url], capture_output=True)
        raw = r.stdout.decode('utf-8', 'replace')
        head, _, body = raw.partition(CRLF + CRLF)
        status = int(head.split()[1]) if head.strip() else 0
        hdr = {k.lower(): v.strip() for k, _, v in (l.partition(':') for l in head.split(CRLF)[1:])}
        if status in (403, 429):
            reset = int(hdr.get('x-ratelimit-reset', str(int(time.time()) + 60)))
            print(f'rate-limited, sleeping {reset - time.time():.0f}s', file=sys.stderr, flush=True)
            time.sleep(max(1, reset - time.time()) + 2); continue
        if status != 200:
            print('HTTP', status, body[:300], file=sys.stderr, flush=True); time.sleep(10); continue
        data = json.loads(body)
        if hdr.get('x-ratelimit-remaining') == '0':
            time.sleep(max(1, int(hdr.get('x-ratelimit-reset', '0')) - time.time()) + 1)
        return data
def q_for(a, b):
    return f'repo:rakudo/rakudo is:issue created:{a}..{b}'
def ranges(a, b):
    n = get(q_for(a, b), per=1)['total_count']
    print(f'{a}..{b}: {n}', file=sys.stderr, flush=True)
    if n <= 1000:
        return [(a, b, n)]
    da = datetime.date.fromisoformat(a); db = datetime.date.fromisoformat(b)
    mid = da + (db - da) / 2
    return ranges(a, mid.isoformat()) + ranges((mid + datetime.timedelta(days=1)).isoformat(), b)
allitems = {}
for a, b, n in ranges('2008-01-01', '2026-12-31'):
    pages = (n + 99) // 100
    for p in range(1, pages + 1):
        d = get(q_for(a, b), page=p)
        for it in d['items']:
            allitems[it['number']] = it
        print(f'  {a}..{b} page {p}/{pages}: total {len(allitems)}', file=sys.stderr, flush=True)
with open(OUT, 'w') as f:
    json.dump(sorted(allitems.values(), key=lambda i: i['number']), f)
print(len(allitems))
