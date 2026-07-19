#!/usr/bin/env raku
# gen-examples.raku — turn ../examples/*.raku into playground/examples.js.
#
# A Raku rewrite of the former gen-examples.py: Raku.js generates its own
# playground data with the very interpreter it ships. examples/ stays the single
# source of truth; the playground never hardcodes Raku source. build.sh runs this
# with the native `rakupp` binary.
#
# The concurrency/IO examples (parallel, sleep-sort, echo-server) are omitted on
# purpose: they need real threads or sockets, which the single-threaded WASM build
# doesn't have — running them would hang the page, so they're not offered at all.

my $here     = $*PROGRAM.IO.parent;
my $exampled = $here.add('../examples');
my $outfile  = $here.add('playground/examples.js');

# (category => [basenames]) in display order; mirrors examples/README.md.
my @categories =
    'Graphics & fractals' => <mandel sierpinski life>,
    'Grammars & parsing'  => <calculator json rpn>,
    'Numbers & maths'     => <primes rationals fibonacci factorize pascal matrix>,
    'Algorithms'          => <quicksort hanoi nqueens brainfuck>,
    'Text & strings'      => <anagrams wordcount cipher roman>,
    'Curiosities'         => <quine>;

# A few gentle warm-up snippets shown first, before the examples/ programs.
my $fizzbuzz = q:to/RAKU/.chomp;
    for 1..20 {
        when $_ %% 15 { say "FizzBuzz" }
        when $_ %%  3 { say "Fizz" }
        when $_ %%  5 { say "Buzz" }
        default        { say $_ }
    }
    RAKU

my @basics =
    'Hello, world' => 'say "Hello, Raku from the browser!";',
    'Ranges & map' => 'say (1..10).map(* ** 2).join(", ");',
    'FizzBuzz'     => $fizzbuzz;

# JSON-encode a string: escape ", \, control chars; raw UTF-8 is valid JSON.
sub json-str(Str() $s --> Str) {
    my $o = '"';
    for $s.comb -> $c {
        given $c {
            when '"'  { $o ~= '\"' }
            when '\\' { $o ~= '\\\\' }
            when "\n" { $o ~= '\n' }
            when "\r" { $o ~= '\r' }
            when "\t" { $o ~= '\t' }
            default {
                $o ~= $c.ord < 0x20 ?? sprintf('\u%04x', $c.ord) !! $c;
            }
        }
    }
    $o ~ '"';
}

# The example's first real comment line (skipping the shebang), for a tooltip.
sub first-doc-line(Str $src --> Str) {
    for $src.lines -> $line {
        my $s = $line.trim;
        next if $s.starts-with('#!');
        return $s.subst(/^ '#' \s* /, '') if $s.starts-with('#');
        last if $s;
    }
    return '';
}

# Serialize one example object with only the fields it has.
sub item-json(%it --> Str) {
    my @parts;
    @parts.push: '"name": '     ~ json-str(%it<name>);
    @parts.push: '"category": ' ~ json-str(%it<category>);
    @parts.push: '"desc": '     ~ json-str(%it<desc>) if %it<desc>;
    @parts.push: '"stdin": '    ~ json-str(%it<stdin>) if %it<stdin>;   # preset standard input
    @parts.push: '"samples": ' ~ %it<samples> if %it<samples>;         # already-JSON array of {name, code}
    @parts.push: '"code": '     ~ json-str(%it<code>);
    '{' ~ @parts.join(', ') ~ '}';
}

# ---- Language showcases (whole interpreters, bundled with a demo program) ----
# From showcase/. Each is a complete interpreter; in the browser we disable its
# CLI `MAIN` (no REPL) and make it read the program to interpret from STANDARD
# INPUT — so the source file lives in the playground's editable stdin box (a
# sample is preset via the example's `stdin` field). Source of truth: showcase/.
my $showcased = $here.add('../showcase');

sub showcase-code(Str $relpath, Str $entry --> Str) {
    my $src = $showcased.add($relpath).slurp;
    $src ~~ s/'sub MAIN'/sub MAIN-cli/;   # don't auto-run the CLI dispatcher (rename so it isn't called)
    $src ~ "\n\n"
        ~ "# ---------- Raku.js playground: interpret the program on standard input ----------\n"
        ~ "# (the CLI reads a file; in the browser the STANDARD INPUT box is that file)\n"
        ~ $entry ~ "\n";
}

# Read one of the showcase's own example programs (showcase/<lang>/examples/…)
# to offer as a sample. Only programs that fit the WASM recursion cap are used.
sub sample-file(Str $relpath --> Str) { $showcased.add($relpath).slurp.chomp }

# Sample programs offered per interpreter (a dropdown in the playground picks
# one into the stdin box). Kept small on purpose: the WASM build caps recursion
# at ~200 C++ frames, so deep recursion (lisp fact > ~18, recursive Forth fib)
# overflows — these all run comfortably under it.
my $lisp-hof = q:to/S/.chomp;
    ; Higher-order functions and an exact numeric tower.
    (define square (lambda (x) (* x x)))
    (display (map square '(1 2 3 4 5))) (newline)
    (define (fact n) (if (< n 2) 1 (* n (fact (- n 1)))))
    (display (fact 12)) (newline)
    S
my $lisp-closures = q:to/S/.chomp;
    ; Closures capture private state.
    (define (make-adder n) (lambda (x) (+ x n)))
    (define add10 (make-adder 10))
    (display (add10 5)) (newline)
    (display (map (make-adder 100) '(1 2 3))) (newline)
    S
my $lisp-fib = q:to/S/.chomp;
    ; Recursion: the first Fibonacci numbers.
    ; (kept shallow — the WASM build caps recursion depth at a few hundred frames)
    (define (fib n) (if (< n 2) n (+ (fib (- n 1)) (fib (- n 2)))))
    (display (map fib '(0 1 2 3 4 5 6))) (newline)
    S

my $forth-arith = q:to/S/.chomp;
    \ New words built from old, run against a shared stack.
    : square ( n -- n² )  dup * ;
    : cube   ( n -- n³ )  dup dup * * ;
    5 square .  cr
    3 cube .    cr
    S
my $forth-loops = q:to/S/.chomp;
    \ Structured loops: sum of squares, then a row of stars.
    : sumsq ( n -- Σi² )  0 swap 1+ 1 do i dup * + loop ;
    : stars ( n -- )      0 do 42 emit loop ;
    10 sumsq .  cr
    8 stars     cr
    S
my $forth-stack = q:to/S/.chomp;
    \ Stack juggling: dup, swap, over, rot.
    1 2 3 + * .   cr        \ 1 * (2 + 3) = 5
    10 20 swap - .  cr      \ 20 - 10 = 10
    7 dup * .  cr           \ 49
    S

my @showcases =
    %( name => 'Lisp interpreter',  file => 'lisp/lisp.raku',
       entry => 'run-source($*IN.slurp, global-env());',
       desc => 'A small Scheme — a Raku grammar reads it, a tree-walker runs it',
       # lisp/examples/ (fact 100, deep closures) overflow the WASM cap — use shallow demos
       samples => ('Higher-order fns' => $lisp-hof, 'Closures' => $lisp-closures, 'Fibonacci' => $lisp-fib) ),
    %( name => 'Forth interpreter', file => 'forth/forth.raku',
       entry => 'run-source($*IN.slurp);',
       desc => 'A stack machine + word dictionary — the other language model',
       samples => ('Full demo' => sample-file('forth/examples/demo.fth'),
                   'Words & arithmetic' => $forth-arith, 'Loops' => $forth-loops, 'Stack juggling' => $forth-stack) ),
    %( name => 'JavaScript / TypeScript', file => 'js/js.raku',
       entry => 'run-program($*IN.slurp);',
       desc => 'A practical JS/TS interpreter — grammar, evaluator, ASI',
       # the real showcase/js/examples/ programs, minus the recursion-heavy ones
       # (fib, quicksort, gameoflife, calculator) that overflow the WASM cap
       samples => ('FizzBuzz'            => sample-file('js/examples/fizzbuzz.js'),
                   'Closures'            => sample-file('js/examples/closures.js'),
                   'Word count'          => sample-file('js/examples/wordcount.js'),
                   'Bitwise, switch, ?.' => sample-file('js/examples/bits.js'),
                   'Bank (TypeScript)'   => sample-file('js/examples/bank.ts'),
                   'Shapes (TypeScript)' => sample-file('js/examples/shapes.ts'),
                   'Roman numerals (TS)' => sample-file('js/examples/roman.ts')) );

# Serialize a showcase's samples as a JSON array of {name, code} objects.
sub samples-json(@samples --> Str) {
    '[' ~ @samples.map(-> $p {
        '{"name": ' ~ json-str($p.key) ~ ', "code": ' ~ json-str($p.value) ~ '}'
    }).join(', ') ~ ']';
}

my @items;
for @basics -> $p {
    @items.push: { name => $p.key, category => 'Basics', code => $p.value };
}
for @showcases -> %sc {
    my $path = $showcased.add(%sc<file>);
    unless $path.e { note "WARNING: showcase not found: %sc<file>"; next }
    my @samples = %sc<samples>.list;
    @items.push: {
        name     => %sc<name>,
        category => 'Language showcases',
        desc     => %sc<desc>,
        stdin    => @samples[0].value,          # first sample preset in the stdin box
        samples  => samples-json(@samples),     # the rest offered in a picker
        code     => showcase-code(%sc<file>, %sc<entry>),
    };
}

my @missing;
for @categories -> $cat {
    for @($cat.value) -> $base {
        my $path = $exampled.add("$base.raku");
        unless $path.e {
            @missing.push: $base;
            next;
        }
        my $code = $path.slurp;
        @items.push: {
            name     => $base,
            category => $cat.key,
            desc     => first-doc-line($code),
            code     => $code,
        };
    }
}

note "WARNING: examples not found: { @missing.join(', ') }" if @missing;

my $js = "// AUTO-GENERATED by rakujs/gen-examples.raku — do not edit.\n"
       ~ "// Source of truth: examples/*.raku\n"
       ~ "window.RAKU_EXAMPLES = [\n"
       ~ @items.map(&item-json).join(",\n")
       ~ "\n];\n";

spurt $outfile, $js;
say "wrote playground/examples.js "
  ~ "({ @items.elems } examples: { @basics.elems } basics, "
  ~ "{ @items.grep({ .<category> ne 'Basics' && .<category> ne 'Language showcases' }).elems } from examples/, "
  ~ "{ @showcases.elems } showcases)";
