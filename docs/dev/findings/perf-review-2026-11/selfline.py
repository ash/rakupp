import re, sys, collections
# Self time per frame (function + file:line) from a macOS `sample` call graph.
path = sys.argv[1]; top = int(sys.argv[2]) if len(sys.argv) > 2 else 40
mode = sys.argv[3] if len(sys.argv) > 3 else "line"
lines = open(path).read().split("\n")
try: s = lines.index("Call graph:") + 1
except ValueError: sys.exit("no call graph")
rx = re.compile(r"^([ +!:|]*)(\d+) (.*)$")
stack = []  # (depth, count, label, childsum)
selfc = collections.Counter(); total = 0
def label(txt):
    fn = re.sub(r"\s+\(in [^)]+\).*", "", txt)
    fn = re.sub(r"\(.*", "", fn) if not fn.startswith("???") else fn
    m = re.search(r"\]\s+(\S+:\d+)\s*$", txt)
    loc = m.group(1) if m else ""
    return fn + (("  " + loc) if mode == "line" and loc else "")
def flush(upto):
    global total
    while stack and stack[-1][0] >= upto:
        d, c, l, cs = stack.pop()
        if c - cs > 0 and "ulock_wait" not in l and "Thread_" not in l and "__psynch" not in l:
            selfc[l] += c - cs; total += c - cs
for ln in lines[s:]:
    if ln.startswith("Total number"): break
    m = rx.match(ln)
    if not m: continue
    d = len(m.group(1)); c = int(m.group(2)); l = label(m.group(3))
    flush(d)
    if stack: stack[-1][3] += c
    stack.append([d, c, l, 0])
flush(-1)
print(f"total worker self samples: {total}")
for l, c in selfc.most_common(top):
    print(f"{100.0*c/total:5.1f}%  {c:5d}  {l[:150]}")
