# Extract runnable Raku snippets from issue bodies. Data in, JSON lines out; nothing here runs code.
import json, re, sys, shlex
issues = json.load(open(sys.argv[1]))
FENCE = re.compile(r'^[ \t]*(```+|~~~+)[ \t]*([\w+.-]*)[^\n]*\n(.*?)^[ \t]*\1[ \t]*$', re.M | re.S)
CMD = re.compile(r'^\s*(?:[$%#>]\s*|\S*[$%#>]\s+)?(?:\S*/)?(raku|perl6|rakudo|rakudo-m|rakudo-moar)(?:\.exe)?\s+(.*)$')
BOT = re.compile(r'^\s*(?:<\w+>\s*)?(m|e|c|camelia|evalable6|committable6|bisectable6|bisect|r|rakudo)\s*:\s+(.*)$')
CODEWORDS = re.compile(r'\b(say|put|print|printf|note|dd|my|our|sub|method|class|role|grammar|use|unit|token|rule|regex|for|given|multi|proto|constant|enum|subset|EVAL|react|start|await)\b|\.say\b|\.put\b')
OUTPUTISH = re.compile(r'^\s*(===SORRY!===|Cannot |Type check failed|No such method|Died|Unhandled exception|Segmentation|  in (block|sub|method)|This type cannot|Too (few|many) positionals|Use of uninitialized|Potential difficulties|MoarVM panic|Malformed|Error while)')
def e_args(argstr):
    try:
        toks = shlex.split(argstr, posix=True)
    except ValueError:
        return None, None
    code = None; flags = []
    i = 0
    while i < len(toks):
        t = toks[i]
        if t in ('-e', '--e') and i + 1 < len(toks):
            code = (code + '\n' if code else '') + toks[i + 1]; i += 2; continue
        m = re.match(r'^-([a-zA-Z]*)e$', t)
        if m and i + 1 < len(toks):
            flags.append('-' + m.group(1)) if m.group(1) else None
            code = (code + '\n' if code else '') + toks[i + 1]; i += 2; continue
        if t.startswith('-') and code is None:
            flags.append(t)
        i += 1
    return code, flags
def from_bot(rest, bot):
    # committable syntax: "c: HEAD,2020.01 say 1"; skip commit spec
    if bot in ('c', 'committable6', 'bisect', 'bisectable6'):
        parts = rest.split(None, 1)
        if len(parts) == 2 and re.match(r'^[\w.,=/-]+$', parts[0]) and not CODEWORDS.match(parts[0]):
            rest = parts[1]
    return rest.strip()
out = []
for it in issues:
    body = (it.get('body') or '').replace('\r\n', '\n')
    snips = []
    spans = []
    for m in FENCE.finditer(body):
        lang = m.group(2).lower(); txt = m.group(3)
        spans.append(m.span())
        if lang and lang not in ('raku', 'perl6', 'p6', 'perl', 'rakudo', 'raku6', 'sh', 'shell', 'bash', 'console', 'text', 'zsh', 'pl', 'pm6', 'rakumod', 'pm', 'raku6', 'perl-6', 'raku-lang', 'rakulang', 'shell-session', 'shellsession', 'plain', 'plaintext', 'none', 'txt', 'sql', 'output'):
            continue
        lines = txt.split('\n')
        cmd_found = False
        for ln in lines:
            cm = CMD.match(ln)
            if cm:
                code, flags = e_args(cm.group(2))
                if code:
                    snips.append({'src': 'cmd', 'code': code, 'flags': [f for f in flags if f], 'lang': lang}); cmd_found = True
                continue
            bm = BOT.match(ln)
            if bm:
                snips.append({'src': 'bot', 'code': from_bot(bm.group(2), bm.group(1)), 'lang': lang}); cmd_found = True
        if cmd_found:
            continue
        if lang in ('sh', 'shell', 'bash', 'console', 'text', 'zsh', 'output', 'txt', 'plain', 'plaintext', 'none', 'sql', 'shell-session', 'shellsession'):
            continue
        stripped = txt.strip()
        if not stripped:
            continue
        # REPL transcript: lines beginning "> " are input
        repl_in = [l[2:] for l in lines if l.startswith('> ')]
        if repl_in and len(repl_in) >= 1 and all((l.startswith('> ') or not CODEWORDS.search(l) or OUTPUTISH.match(l)) for l in lines if l.strip()) and not stripped.startswith('>>'):
            snips.append({'src': 'repl', 'code': ';\n'.join(repl_in), 'lang': lang}); continue
        if OUTPUTISH.match(stripped):
            continue
        if lang in ('raku', 'perl6', 'p6', 'rakudo', 'raku6', 'pm6', 'rakumod', 'perl-6', 'raku-lang', 'rakulang') or CODEWORDS.search(stripped):
            snips.append({'src': 'block', 'code': txt.rstrip('\n'), 'lang': lang})
    # inline (outside fences): bot lines and backticked one-liners
    rest = body
    for a, b in reversed(spans):
        rest = rest[:a] + rest[b:]
    for ln in rest.split('\n'):
        bm = BOT.match(ln.strip('`> '))
        if bm and bm.group(1) in ('m', 'e', 'c', 'camelia', 'evalable6', 'committable6', 'bisectable6', 'bisect', 'r'):
            snips.append({'src': 'bot-inline', 'code': from_bot(bm.group(2).strip('`'), bm.group(1))})
        for cm in re.finditer(r'`((?:raku|perl6|rakudo)\s+[^`]*-\w*e\s+[^`]+)`', ln):
            code, flags = e_args(cm.group(1).split(None, 1)[1])
            if code:
                snips.append({'src': 'cmd-inline', 'code': code, 'flags': [f for f in flags if f]})
    seen = set(); uniq = []
    for s in snips:
        k = s['code'].strip()
        if k and k not in seen and len(k) < 20000:
            seen.add(k); uniq.append(s)
    out.append({'number': it['number'], 'nsnips': len(uniq), 'snips': uniq})
with open(sys.argv[2], 'w') as f:
    for o in out:
        f.write(json.dumps(o) + '\n')
n = sum(1 for o in out if o['nsnips']); t = sum(o['nsnips'] for o in out)
from collections import Counter
c = Counter(s['src'] for o in out for s in o['snips'])
print(f'{n} issues with snippets, {t} snippets', dict(c))
