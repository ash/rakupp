// smoke.cjs — run a handful of programs through a Raku.js build and check the
// output, from Node. The point is the class of bug a native test cannot see:
// the WASM build is single-threaded, so anything the engine does with a thread
// behind the scenes fails there and nowhere else. One such path (the grammar
// memo reaper) aborted every non-trivial parse in the browser while every
// native gate stayed green — the showcase interpreters in the playground all
// died on `Aborted()`. This runs the same interpreters the playground ships.
//
//   node --stack-size=6000 rakujs/smoke.cjs <dir-with-rakujs.js>
//
// The build must include `node` in -sENVIRONMENT:
//   RAKUJS_ENV=node,web,worker RAKUJS_OUT=/tmp/rakujs-node rakujs/build.sh
//
// Exits 0 when every case matches, 1 (with a diff) on the first failure.
const path = require('path');
const fs   = require('fs');

const dir = process.argv[2] || path.join(__dirname, 'playground');
const RakuJS = require(path.join(dir, 'rakujs.js'));
const SHOWCASE = path.join(__dirname, '..', 'showcase');

// A showcase interpreter, wired the way gen-examples.raku wires it for the
// playground: no CLI dispatcher, the program to interpret arrives on stdin.
function showcase(relpath, entry) {
    const src = fs.readFileSync(path.join(SHOWCASE, relpath), 'utf8')
                  .replace('sub MAIN', 'sub MAIN-cli');
    return src + '\n' + entry + '\n';
}

const CASES = [
    { name: 'say',   src: 'say "hello";', want: 'hello\n' },
    { name: 'grammar', want: 'ok\n',
      src: 'grammar G { token TOP { <w>+ % "," } token w { \\w+ } }\n' +
           'say G.parse(("ab" xx 400).join(",")) ?? "ok" !! "no parse";' },
    { name: 'lisp',  src: showcase('lisp/lisp.raku', 'run-source($*IN.slurp, global-env());'),
      stdin: '(display (map (lambda (x) (* x x)) (list 1 2 3)))', want: '(1 4 9)' },
    { name: 'js',    src: showcase('js/js.raku', 'run-program($*IN.slurp);'),
      stdin: 'const a = [{ n: 21 }];\nconsole.log(a[0].n * 2);', want: '42\n' },
    { name: 'perl',  src: showcase('perl/perl.raku', 'run-perl($*IN.slurp);'),
      stdin: 'my @n = (3, 1, 2);\nprint join(",", sort { $a <=> $b } @n), "\\n";', want: '1,2,3\n' },
    { name: 'python', src: showcase('python/python.raku', 'run-python($*IN.slurp);'),
      stdin: 'xs = [3, 1, 2]\nxs.sort()\nprint(xs)', want: '[1, 2, 3]\n' },
];

(async () => {
    let fails = 0;
    for (const c of CASES) {
        let out = '', err = '';
        const m = await RakuJS({ print: t => { out += t + '\n' }, printErr: t => { err += t + '\n' } });
        let rc = -1;
        try {
            rc = m.ccall('rakupp_run', 'number', ['string', 'string'], [c.src, c.stdin || '']);
        } catch (e) {
            err += String(e) + '\n';
        }
        const ok = rc === 0 && out.startsWith(c.want);
        console.log(`${ok ? 'ok' : 'not ok'} - ${c.name}`);
        if (!ok) {
            fails++;
            console.log(`#   exit ${rc}`);
            console.log(`#   want: ${JSON.stringify(c.want)}`);
            console.log(`#   got:  ${JSON.stringify(out.slice(0, 200))}`);
            if (err) console.log(`#   err:  ${JSON.stringify(err.slice(0, 300))}`);
        }
    }
    console.log(`1..${CASES.length}`);
    if (fails) { console.log(`# ${fails} of ${CASES.length} failed`); process.exit(1) }
    console.log('# all wasm smoke cases passed');
})();
