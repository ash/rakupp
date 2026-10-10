#!/usr/bin/env rakupp
# The scaling gate: loading a program, and the common things a program does,
# must cost time LINEAR in their size.
#
# Issue #141 is why this exists. A lexer check added for 5.3.0 searched the
# whole source once per word after a term, which made every statement re-read
# the file: 3000 one-line subs went from 0.04 s to 0.19 s, 30000 took 16 s.
# Nothing noticed before a user did. Roast runs short files, t/run.raku checks
# output and not time, and tools/perf-guard.raku times fixed workloads on one
# machine, which is the right instrument for a 5% slowdown and the wrong one
# for "this got quadratic": at the size it happens to run, a quadratic is only
# some percent. What exposes one is GROWTH. Each shape here is run at a size n
# and at 8n; linear work takes about 8x as long, quadratic about 64x. That
# ratio does not depend on how fast the machine is, so the gate is the same on
# a laptop, a loaded CI runner and real RISC-V hardware.
#
# Two domains:
#   load  n statements of one shape — declarations that are never called, so
#         the time is the lexer, the parser, the checks and the declaring
#   run   a program that does n of one thing at run time — push, insert,
#         append, split, match, call
#
# How a shape is measured:
#   - every run is a fresh process on the generated file. A load shape is
#     timed from outside, less the best time of a `say 'ok'` program (process
#     startup); a run shape times its own work with `now`, so neither startup
#     nor its own parse is in the number
#   - n starts at the shape's own size and doubles until one run takes 15 ms
#     (load) or 5 ms (run), at most 64x, and halves while it takes more than
#     300 ms: a fast machine measures bigger sizes and a slow one smaller, no
#     shape is too short to time and none is slow to start with
#   - the short size is then the best of three runs; the long one (8n) is run
#     up to three times and passes as soon as one run is in bounds, so a load
#     spike can delay the verdict but not turn it red. A long run is stopped
#     once it is past what could still pass, and that counts as one slow run.
#     A known shape gets one long run: its verdict is a TODO either way
#   - superlinear means the long run grew more than 24x (three times linear)
#     AND took 100 ms or more. On an M-series Mac (2026-10-10) the linear
#     shapes grew a median 8.7x and at most 12.4x — the top is memory, a hash
#     or array that outgrew the caches — and the quadratics grew 41-78x, and
#     v5.3.0's #141 shapes 24-60x
#
# A shape marked `known` is superlinear today for a reason written beside it.
# It is still measured, and reported as a TAP TODO: it does not fail the gate,
# and when it turns linear the summary says so, so the mark can be removed.
#
#   rakupp t/scaling/run.raku                    # every shape, TAP
#   rakupp t/scaling/run.raku -v                 # ...with each size and time on stderr
#   rakupp t/scaling/run.raku --only=postfix,hash
#   rakupp t/scaling/run.raku --domain=load      # or run
#   rakupp t/scaling/run.raku --engine=/path/to/other/rakupp   # judge another build
#   rakupp t/scaling/run.raku --list             # the shapes, one per line
#   rakupp t/scaling/run.raku --once --engine=rakudo   # each shape once, small: is it valid Raku?
#
# Adding a shape: a `load` item is one statement with @I@ for its index; a
# `work` program does @N@ of something. Each must print nothing, or end its
# output with `ok` (the gate appends `say 'ok'` and checks for it, so a shape
# that stops parsing is a failure, not a fast pass). Pick a starting size that
# runs in a few milliseconds; calibration does the rest. Then run it under
# `--once --engine=rakudo`: a shape should be Raku, not merely rakupp, and that
# mode also fails on any warning a shape prints.
#
# Exit 1 when any unmarked shape is superlinear or fails to run.

my $ENGINE  = $*EXECUTABLE.Str;
my @ONLY;
my $DOMAIN  = '';
my $VERBOSE = False;
my $LIST    = False;
my $ONCE    = False;
my $K       = 8;         # growth factor between the two sizes
for @*ARGS -> $a {
    if    $a.starts-with('--engine=') { $ENGINE = $a.substr(9) }
    elsif $a.starts-with('--only=')   { @ONLY = $a.substr(7).split(',').grep(*.chars) }
    elsif $a.starts-with('--domain=') { $DOMAIN = $a.substr(9) }
    elsif $a.starts-with('--k=')      { $K = $a.substr(4).Int }
    elsif $a eq '-v' || $a eq '--verbose' { $VERBOSE = True }
    elsif $a eq '--list'              { $LIST = True }
    elsif $a eq '--once'              { $ONCE = True }
    else {
        note "unknown option: $a";
        exit 2;
    }
}
my $MAX-RATIO = 3 * $K;       # 24 at k = 8
my %FLOOR-MS  = load => 15, run => 5;   # the short run is grown to at least this much
my $CEIL-MS   = 300;          # ...and shrunk while it takes more than this
my $JUDGE-MS  = 100;          # a long run faster than this is never called superlinear
my $GROW-CAP  = 64;           # n moves at most this far from the shape's own size

# ---- the shapes ----------------------------------------------------------

my @SHAPES;

# n statements of one shape, between a prologue and an epilogue
sub load(Str $name, Str $item, Str :$pre = '', Str :$post = '', Int :$n = 1000, Str :$known) {
    @SHAPES.push: %(
        :domain<load>, :$name, :$n, :$known,
        gen => -> Int $count {
            $pre ~ (^$count).map({ $item.subst('@I@', $_, :g) ~ "\n" }).join ~ $post ~ "say 'ok';\n"
        },
    );
}

# a program doing @N@ of one thing, timing itself
sub work(Str $name, Str $code, Int :$n = 10_000, Str :$known) {
    @SHAPES.push: %(
        :domain<run>, :$name, :$n, :$known,
        gen => -> Int $count {
            "my \$SCALING-T0 = now;\n" ~ $code.subst('@N@', $count, :g) ~
            "\nsay 'TIME ', (now - \$SCALING-T0) * 1000;\nsay 'ok';\n"
        },
    );
}

# -- load: statement modifiers. Every word after a term, which is where #141 was
load 'postfix-if',      Q｢sub f@I@() { my $r = 1 if @I@ > 3; $r }｣;
load 'postfix-unless',  Q｢sub f@I@() { my $r = 1 unless @I@ > 3; $r }｣;
load 'postfix-with',    Q｢sub f@I@() { my $r = 1 with @I@; $r }｣;
load 'postfix-without', Q｢sub f@I@() { my $r = 1 without @I@; $r }｣;
load 'postfix-while',   Q｢sub f@I@() { my $r = 0; $r = 1 while $r < 0 - @I@; $r }｣;
load 'postfix-until',   Q｢sub f@I@() { my $r = 0; $r = 1 until $r > -1 - @I@; $r }｣;
load 'postfix-for',     Q｢sub f@I@() { .say for 1..@I@; 2 }｣;
load 'postfix-given',   Q｢sub f@I@() { .say given @I@; 2 }｣;
load 'return-if',       Q｢sub f@I@($x) { return 1 if $x > @I@; 2 }｣;

# -- load: word and symbol infixes
load 'and-or',          Q｢sub f@I@() { 1 and @I@ or 3 }｣;
load 'xx',              Q｢sub f@I@() { 1 xx @I@ }｣;
load 'eq-ne',           Q｢sub f@I@() { 'a' eq 'b' ne 'c' }｣;
load 'cmp-leg',         Q｢sub f@I@() { (1 cmp @I@) ~ ('a' leg 'b') }｣;
load 'div-mod',         Q｢sub f@I@() { @I@ div 2 + @I@ mod 3 }｣;
load 'x-repeat',        Q｢sub f@I@() { 'a' x @I@ }｣;
load 'but',             Q｢sub f@I@() { 1 but True }｣;
load 'min-max',         Q｢sub f@I@() { (1 min @I@) max 3 }｣;
load 'ternary',         Q｢sub f@I@() { @I@ > 3 ?? 1 !! 2 }｣;
load 'smartmatch',      Q｢sub f@I@() { @I@ ~~ Int }｣;
load 'division',        Q｢sub f@I@() { @I@ / 3 }｣;
load 'modulo',          Q｢sub f@I@($x) { $x % 3 + $x %% 2 }｣;
load 'listop-division', Q｢sub f@I@() { elems(@*ARGS) / 2 }｣;
load 'comparisons',     Q｢sub f@I@($x) { $x < @I@ && $x > 0 }｣;

# -- load: quotes and regexes
load 'regex-match',     Q｢sub f@I@() { 'abc' ~~ /b/ }｣;
load 'regex-m',         Q｢sub f@I@() { 'abc' ~~ m/b/ }｣;
load 'regex-rx',        Q｢sub f@I@() { rx/ a+ b* / }｣;
load 'subst',           Q｢sub f@I@() { my $s = 'abc'; $s ~~ s/b/c/ }｣;
load 'trans',           Q｢sub f@I@() { my $s = 'abc'; $s ~~ tr/a/b/ }｣;
load 'interpolation',   Q｢sub f@I@($a, %h, @l) { "$a @I@ %h<k> @l[0] {$a} $a.uc()" }｣;
load 'qw',              Q｢sub f@I@() { <a b c @I@> }｣;
load 'q-qq',            Q｢sub f@I@() { q{a @I@} ~ qq{b @I@} }｣;
load 'strings',         Q｢sub f@I@() { 'it\'s' ~ "q@I@" ~ Q[raw] }｣;
load 'heredoc',         "sub f\@I\@() \{ q:to/END/;\n  text \@I\@\n  END\n}";
load 'unicode',         Q｢sub f@I@() { my $ü = 'ä@I@'; $ü ~ '→' }｣;

# -- load: block forms
load 'block-if-else',   Q｢sub f@I@() { if @I@ > 3 { 1 } else { 2 } }｣;
load 'block-for',       Q｢sub f@I@() { for 1..@I@ -> $x { $x } }｣;
load 'block-while',     Q｢sub f@I@() { my $x = 0; while $x < @I@ { $x++ } }｣;
load 'given-when',      Q｢sub f@I@() { given @I@ { when 1 { 2 }; default { 3 } } }｣;
load 'loop',            Q｢sub f@I@() { loop (my $j = 0; $j < @I@; $j++) { } }｣;
load 'try-catch',       Q｢sub f@I@() { try { die 'x' }; CATCH { default { } } }｣;
load 'phasers',         Q｢sub f@I@() { my $x; LEAVE { $x = 1 }; ENTER { 2 }; $x }｣;
load 'pointy-map',      Q｢sub f@I@(@a) { @a.map(-> $x { $x * @I@ }) }｣;
load 'whatever-code',   Q｢sub f@I@() { (1..3).map(* + @I@) }｣;

# -- load: terms and declarations
load 'plain',           Q｢sub f@I@() { @I@ }｣;
load 'my-variable',     Q｢sub f@I@() { my $x = @I@; $x }｣;
load 'my-hash',         Q｢sub f@I@() { my %h; my %g; my %k; @I@ }｣;
load 'hash-literal',    Q｢sub f@I@() { my %h = a => 1, b => @I@; %h<a> }｣;
load 'array-literal',   Q｢sub f@I@() { my @a = 1, 2, @I@; @a[0] }｣;
load 'pairs',           Q｢sub f@I@() { :a(1), :b(@I@), :!c }｣;
load 'subscripts',      Q｢sub f@I@(%h, @a) { %h<a@I@> + %h{'b'} + @a[@I@] + @a[*-1] }｣;
load 'method-chain',    Q｢sub f@I@($s) { $s.trim.lc.split(',').map(*.uc).join('-') }｣;
load 'concat',          Q｢sub f@I@() { 'a' ~ @I@ }｣;
load 'return',          Q｢sub f@I@() { return @I@ }｣;
load 'signature',       Q｢sub f@I@(Int $a where * > 0, Str :$b = 'x', *@rest) { 1 }｣;
load 'call-args',       Q｢sub f@I@($a, $b?, :$c) { f@I@(1, :c(2)) }｣;
load 'call-sub',        Q｢sub f@I@() { 1 }; sub g@I@() { f@I@() }｣;
load 'call-no-parens',  Q｢sub f@I@($x) { g@I@ $x, 1 }; sub g@I@($a, $b) { $a }｣;
load 'export',          Q｢sub f@I@($x) is export { $x }｣;
load 'multi-subs',      Q｢multi sub g@I@(Int $a) { 1 }; multi sub g@I@(Str $a) { 2 }｣;
load 'constant',        Q｢constant C@I@ = @I@;｣;
load 'use-constant',    Q｢constant C@I@ = @I@; sub f@I@() { C@I@ + 1 }｣;
load 'enum',            Q｢enum E@I@ <A@I@ B@I@>;｣;
load 'subset',          Q｢subset S@I@ of Int where * > @I@;｣;
load 'class',           Q｢class C@I@ { has $.a = @I@; method m() { $!a } }｣;
load 'class-attrs',     Q｢class K@I@ { has $.a; has @.b; has %.c; method x { $!a } }｣;
load 'role',            Q｢role R@I@ { method m() { 1 } }｣;
load 'class-does-role', Q｢role R@I@ { method m() { 1 } }; class C@I@ does R@I@ { }｣;
load 'grammar',         Q｢grammar G@I@ { token TOP { <a>+ }; token a { \w } }｣;
load 'regex-decl',      Q｢my regex r@I@ { a+ b* }｣;
load 'operator-decl',   Q｢sub infix:<op@I@>($a, $b) { $a + $b }｣, :n(300);

# -- load: comments and pod
load 'comments',        "# comment \@I\@\nsub f\@I\@() \{ 1 } # trailing";
load 'decl-comments',   "#| about f\@I\@\nsub f\@I\@(\$x) \{ \$x }";
load 'pod-blocks',      "=begin pod\ntext \@I\@\n=end pod\nsub f\@I\@() \{ 1 }";

# -- load: top-level statements, which also run
load 'top-my',          Q｢my $v@I@ = @I@;｣;
load 'top-our',         Q｢our $v@I@ = @I@;｣;
load 'top-assign',      Q｢my $v@I@ = @I@; $v@I@ = $v@I@ + 1;｣;
load 'top-postfix-if',  Q｢my $v@I@ = 1 if @I@ > 3;｣;
load 'top-calls',       Q｢f(@I@);｣, :pre("sub f(\$x) \{ \$x }\n");
load 'top-method-calls', Q｢$o.m(@I@);｣, :pre("class C \{ method m(\$x) \{ \$x } }\nmy \$o = C.new;\n");
load 'top-sub-and-call', Q｢sub f@I@() { @I@ }; f@I@();｣;

# -- load: many statements in ONE scope
load 'scope-postfix-if', Q｢  $x = 1 if @I@ > 3;｣, :pre("sub big() \{\n  my \$x;\n"), :post("}\n");
load 'scope-my',         Q｢  my $x@I@ = @I@; $x@I@++;｣, :pre("sub big() \{\n"), :post("}\n");
load 'scope-blocks',     Q｢  { my $y = @I@; }｣, :pre("sub big() \{\n"), :post("}\n");
load 'scope-if-elsif',   Q｢  if $x > @I@ { $x++ } elsif $x < 0 { $x-- } else { $x = 0 }｣,
     :pre("sub big() \{\n  my \$x = 0;\n"), :post("}\n");

# -- load: ONE construct with many members
load 'class-methods',   Q｢  method m@I@($x) { $x + @I@ }｣, :pre("class C \{\n"), :post("}\n");
load 'given-whens',     Q｢  when @I@ { 1 }｣, :pre("sub big(\$x) \{\n given \$x \{\n"), :post("  default \{ 0 }\n }\n}\n");
load 'elsif-chain',     Q｢elsif $x == @I@ { 1 }｣, :pre("sub big(\$x) \{\nif \$x == -1 \{ 0 }\n"), :post("else \{ 2 }\n}\n");
load 'enum-values',     Q｢a@I@｣, :pre('enum E <'), :post(">;\n");
load 'regex-alternation', Q｢| a@I@｣, :pre('my $r = rx/ x '), :post("/;\n");
load 'long-expression', Q｢+ @I@｣, :pre('my $x = 0 '), :post(";\n");
load 'long-list',       Q｢@I@,｣, :pre('my @x = '), :post("0;\n");
load 'long-string',     Q｢abc@I@｣, :pre("my \$x = 'x"), :post("';\n");
load 'long-comment',    Q｢# @I@｣;
load 'long-pod',        Q｢text @I@｣, :pre("=begin pod\n"), :post("=end pod\n");
load 'class-attributes', Q｢  has $.a@I@ = @I@;｣, :pre("class C \{\n"), :post("}\n"), :n(500),
     :known('parseClass checks each public attribute against every earlier one for an accessor clash');
load 'grammar-tokens',  Q｢  token t@I@ { 'a@I@' }｣, :pre("grammar G \{\n  token TOP \{ <t0> }\n"), :post("}\n"), :n(500),
     :known('composing a grammar grows with its token count squared');
load 'multi-candidates', Q｢multi sub g(Int $a, @I@) { 1 }｣, :n(500),
     :known('a new multi candidate is compared with every earlier one of its name (execDeclStmt `joins`)');
load 'multi-method-candidates', Q｢  multi method m(Int $x, @I@) { $x }｣, :pre("class C \{\n"), :post("}\n"), :n(200),
     :known('each multi method candidate re-filters every earlier one (sigKeyParams remove_if)');

# -- run: arrays
work 'array-push',       Q｢my @a; @a.push($_) for ^@N@;｣;
work 'array-append',     Q｢my @a; @a.append(1, 2, 3) for ^@N@;｣;
work 'array-unshift',    Q｢my @a; @a.unshift($_) for ^@N@;｣, :n(2000),
     :known('unshift and prepend move every element: O(n) per call');
work 'array-shift',      Q｢my @a = ^@N@; @a.shift while @a;｣;
work 'array-pop',        Q｢my @a = ^@N@; @a.pop while @a;｣;
work 'array-index',      Q｢my @a = ^@N@; my $s = 0; $s += @a[$_] for ^@N@;｣;
work 'array-assign-idx', Q｢my @a; @a[$_] = $_ for ^@N@;｣;
work 'array-of-arrays',  Q｢my @m; @m.push([$_, $_]) for ^@N@;｣;
work 'array-nested-push', Q｢my @a = [] xx 10; @a[$_ % 10].push($_) for ^@N@;｣;
work 'array-splice-end', Q｢my @a; @a.splice(@a.elems, 0, $_) for ^@N@;｣, :n(5000);
work 'list-ops',         Q｢my @a = (^@N@).reverse; my @s = @a.sort; my @u = @s.unique; my $j = @u.join(',');｣;
work 'map-grep',         Q｢my $c = (^@N@).map(* * 2).grep(* %% 3).elems;｣;
work 'gather-take',      Q｢my @g = gather { take $_ for ^@N@ };｣;
work 'lazy-head',        Q｢my $s = 0; for (1..*).head(@N@) { $s += $_ }｣;
work 'reduce',           Q｢my $s = [+] ^@N@; my $p = (^@N@).sum;｣, :n(50_000);
work 'zip',              Q｢my $z = (^@N@ Z ^@N@).elems;｣;
work 'slices',           Q｢my @a = ^@N@; my @b = @a[^(@N@ div 2)]; my @c = @a[(@N@ div 2) .. *];｣;
work 'array-iteration',  Q｢my @a = ^@N@; my $s = 0; for @a.kv -> $k, $v { $s += $k + $v }｣;

# -- run: hashes and sets
work 'hash-insert',      Q｢my %h; %h{$_} = $_ for ^@N@;｣;
work 'hash-insert-str',  Q｢my %h; %h{"k$_"} = 1 for ^@N@;｣;
work 'hash-lookup',      Q｢my %h = (^@N@).map({ $_ => $_ }); my $s = 0; $s += %h{$_} for ^@N@;｣;
work 'hash-delete',      Q｢my %h = (^@N@).map({ $_ => $_ }); %h{$_}:delete for ^@N@;｣;
work 'hash-exists',      Q｢my %h = (^@N@).map({ $_ => 1 }); my $c = 0; $c++ if %h{$_}:exists for ^@N@;｣;
work 'hash-iteration',   Q｢my %h = (^@N@).map({ $_ => $_ }); my $s = 0; $s += .value for %h; $s += $_ for %h.keys;｣;
work 'hash-of-arrays',   Q｢my %h; %h{$_ % 100}.push($_) for ^@N@;｣;
work 'hash-nested',      Q｢my %h; %h{$_ % 100}{$_} = 1 for ^@N@;｣;
work 'set-bag',          Q｢my $s = set ^@N@; my $b = bag (^@N@).map(* % 100); my $c = 0; $c++ if $s{$_} for ^@N@;｣;
work 'set-elem',         Q｢my $s = set ^@N@; my $c = 0; $c++ if $_ (elem) $s for ^@N@;｣, :n(1000),
     :known('`(elem)` costs O(set size) per test where `$set{$x}` is a hash lookup');
work 'set-hash',         Q｢my %s is SetHash; %s{$_} = True for ^@N@;｣;

# -- run: strings
work 'str-append',       Q｢my $s = ''; $s ~= 'abcde' for ^@N@;｣;
work 'str-append-chars', Q｢my $s = ''; my $c = 0; for ^@N@ { $s ~= 'a'; $c += $s.chars }｣;
work 'str-append-unicode', Q｢my $s = 'é'; $s ~= 'é' for ^@N@;｣, :n(1000),
     :known('joining non-ASCII text re-normalizes the WHOLE result to NFC: nfcNormalize(dst + v) per append');
work 'str-append-chars-unicode', Q｢my $s = 'é'; my $c = 0; for ^@N@ { $s ~= 'é'; $c += $s.chars }｣, :n(1000),
     :known('the same whole-string NFC pass per append');
work 'str-prepend',      Q｢my $s = ''; $s = 'abcde' ~ $s for ^@N@;｣;
work 'str-join',         Q｢my $j = (^@N@).join(',');｣, :n(50_000);
work 'str-split',        Q｢my $s = 'ab,' x @N@; my $c = $s.split(',').elems;｣, :n(50_000);
work 'str-comb',         Q｢my $c = ('ab' x @N@).comb.elems;｣, :n(50_000);
work 'str-lines-words',  Q｢my $l = ("line\n" x @N@).lines.elems; my $w = ("w " x @N@).words.elems;｣, :n(50_000);
work 'str-substr-scan',  Q｢my $s = 'abcdefghij' x (@N@ div 10); my $c = 0; for ^$s.chars { $c++ if $s.substr($_, 1) eq 'a' }｣;
work 'str-substr-scan-unicode', Q｢my $s = 'é' ~ ('abcdefghij' x (@N@ div 10)); my $c = 0; for ^$s.chars { $c++ if $s.substr($_, 1) eq 'a' }｣;
work 'str-index-walk',   Q｢my $s = 'ab' x @N@; my $p = 0; my $c = 0; while ($p = $s.index('b', $p)).defined { $c++; $p++ }｣, :n(2000),
     :known('`.index` with a start position costs O(string length) per call; without one it does not');
work 'str-ord-scan',     Q｢my $s = 'abcdefghij' x (@N@ div 10); my $c = 0; for ^$s.chars { $c += $s.substr($_, 1).ord }｣;
work 'str-match-global', Q｢my $c = +(('ab' x @N@) ~~ m:g/b/);｣;
work 'str-match-continue', Q｢my $s = 'ab' x @N@; my $c = 0; $c++ while $s ~~ m:c/b/;｣, :n(2000),
     :known('a `m:c` match continuing from $/.to pays for the position it starts at');
work 'str-subst-global', Q｢my $t = ('ab' x @N@).subst('b', 'c', :g); my $u = 'ab' x @N@; $u ~~ s:g/a/x/;｣;
work 'str-trans',        Q｢my $t = ('ab' x @N@).trans('a' => 'x');｣, :n(50_000);
work 'str-sort',         Q｢my @s = (^@N@).map(*.Str).sort;｣;
work 'str-interpolate',  Q｢my $s; for ^@N@ { $s = "a $_ b {$_ + 1}" }｣;
work 'str-sprintf',      Q｢my $s; for ^@N@ { $s = sprintf('%05d %s', $_, 'x') }｣;
work 'str-concat-reduce', Q｢my $s = [~] (^@N@).map(*.Str);｣, :n(2000),
     :known('`[~]` folds pairwise, copying the growing string each step; `.join` is linear');
work 'str-encode-decode', Q｢my $s = 'aé' x @N@; my $b = $s.encode; my $t = $b.decode;｣, :n(50_000);
work 'buf-concat',       Q｢my $c = Buf.new; $c ~= Buf.new(1, 2) for ^@N@;｣;
work 'buf-push',         Q｢my $b = Buf.new; $b.push(1) for ^@N@;｣, :n(20_000),
     :known('`.push` on a Buf costs more as the Buf grows (80k -> 320k pushes: 8.5x the time); `~=` does not');
work 'buf-append',       Q｢my $c = Buf.new; $c.append(1, 2) for ^@N@;｣, :n(5000),
     :known('`.append` on a Buf costs more as the Buf grows (20k -> 80k calls: 21x the time)');

# -- run: objects, calls, control
work 'object-new',       Q｢class P { has $.x }; my @o = (^@N@).map({ P.new(x => $_) });｣;
work 'method-calls',     Q｢class P { method m($x) { $x } }; my $o = P.new; my $s = 0; $s += $o.m($_) for ^@N@;｣;
work 'attr-array-push',  Q｢class Coll { has @.items; method add($x) { @!items.push($x) } }; my $q = Coll.new; $q.add($_) for ^@N@;｣;
work 'attr-hash-insert', Q｢class Coll { has %.h; method add($x) { %!h{$x} = 1 } }; my $q = Coll.new; $q.add($_) for ^@N@;｣;
work 'closures',         Q｢my @c = (^@N@).map(-> $i { -> { $i } }); my $s = 0; $s += .() for @c;｣;
work 'multi-dispatch',   Q｢multi f(Int $x) { 1 }; multi f(Str $x) { 2 }; my $s = 0; $s += f($_) for ^@N@;｣;
work 'exceptions',       Q｢my $c = 0; for ^@N@ { try { die 'x' }; $c++ if $! }｣, :n(2000);
work 'given-when-loop',  Q｢my $c = 0; for ^@N@ { given $_ % 3 { when 0 { $c++ }; default { } } }｣;
work 'junction',         Q｢my $c = 0; $c++ if $_ == any(1, 2, 3) for ^@N@;｣;
work 'rat-sum',          Q｢my $r = 0; $r += 1 / ($_ % 7 + 1) for ^@N@;｣;
work 'channel',          Q｢my $ch = Channel.new; $ch.send($_) for ^@N@; $ch.close; my $s = 0; $s += $_ for $ch.list;｣;
work 'supply-tap',       Q｢my $c = 0; Supply.from-list(^@N@).tap({ $c++ });｣;
work 'eval-small',       Q｢use MONKEY-SEE-NO-EVAL; my $s = 0; $s += EVAL('1') for ^@N@;｣, :n(500);

# ---- selection -----------------------------------------------------------

my @run = @SHAPES.grep({
    (!$DOMAIN || .<domain> eq $DOMAIN) &&
    (!@ONLY || @ONLY.first(-> $o { "{.<domain>}/{.<name>}".contains($o) }).defined)
});
if $LIST {
    say "{.<domain>}/{.<name>}{.<known> ?? '   (known: ' ~ .<known> ~ ')' !! ''}" for @run;
    exit 0;
}
unless @run {
    note "no shape matches";
    exit 2;
}

# ---- measuring -----------------------------------------------------------

my $SCRATCH = $*TMPDIR.add("rakupp-scaling-$*PID");
mkdir $SCRATCH;
END {
    if $SCRATCH.defined && $SCRATCH.d {
        .unlink for $SCRATCH.dir;
        $SCRATCH.rmdir;
    }
}

# One run of `$ENGINE $file`: (seconds, verdict, stdout, stderr), where the
# verdict is 'ok', 'timeout' or the reason it failed. Stdin is a pipe closed at once, and
# the tap blocks end in Nil (tools/run-roast.raku has the measurements for both).
sub run-once(IO::Path $file, Real $timeout) {
    my $proc = Proc::Async.new($ENGINE, $file.Str, :w);
    my $out = '';
    my $err = '';
    $proc.stdout.tap(-> $c { $out ~= $c; Nil });
    $proc.stderr.tap(-> $c { $err ~= $c; Nil });
    my $t0 = now;
    my $done = $proc.start;
    $proc.close-stdin;
    await Promise.anyof($done, Promise.in($timeout));
    my $secs = now - $t0;
    if $done.status ne 'Kept' {
        $proc.kill(SIGKILL);
        return ($secs, 'timeout', $out, $err);
    }
    my $exit = $done.result.exitcode;
    return ($secs, 'ok', $out, $err) if $exit == 0 && $out.lines.tail eq 'ok';
    my $why = $err.lines.first(*.trim.chars) // $out.lines.tail // '';
    ($secs, "exit $exit: {$why.substr(0, 160)}", $out, $err)
}

my %files;
sub file-for(%shape, Int $n) {
    %files{"{%shape<domain>}-{%shape<name>}-$n"} //= do {
        my $f = $SCRATCH.add("{%shape<domain>}-{%shape<name>}-$n.raku");
        $f.spurt: %shape<gen>($n);
        $f
    }
}

# Process startup, which every run pays and no shape is made of
my $BASE = do {
    my $f = $SCRATCH.add('empty.raku');
    $f.spurt: "say 'ok';\n";
    my @t = (^5).map({ run-once($f, 30)[0] });
    @t.min
};

# Up to $reps runs at size $n, stopping early once one takes $enough ms or less:
# (best milliseconds, verdict, best wall seconds). A run that times out counts
# as an infinitely slow one, and two of them end the measurement; a run that
# fails ends it at once with its reason.
sub measure(%shape, Int $n, Int $reps, Real $timeout, Real :$enough = 0) {
    my $file = file-for(%shape, $n);
    my $best = Inf;
    my $wall = Inf;
    my $timeouts = 0;
    for ^$reps {
        my ($secs, $verdict, $out) = run-once($file, $timeout);
        if $verdict eq 'timeout' {
            last if ++$timeouts == 2;
            next;
        }
        return (Inf, $verdict, Inf) if $verdict ne 'ok';
        my $ms = %shape<domain> eq 'run'
            ?? +($out.lines.first(*.starts-with('TIME ')) // 'TIME 0').substr(5)
            !! ($secs - $BASE) * 1000;
        $best min= max($ms, 0.1);
        $wall min= $secs;
        last if $best <= $enough;
    }
    $best == Inf ?? (Inf, 'timeout', Inf) !! ($best, 'ok', $wall)
}

sub fmt-n(Int $n) { $n >= 1000 && $n %% 1000 ?? "{$n div 1000}k" !! ~$n }

# ---- the run -------------------------------------------------------------

my $START = now;
say "1..{+@run}";
note "# engine: $ENGINE (startup {($BASE * 1000).fmt('%.1f')} ms)" if $VERBOSE;
my $test = 0;
my @bad;
my @known-bad;
my @known-fixed;
for @run -> %s {
    $test++;
    my $label = "{%s<domain>}/{%s<name>}";
    my $todo = %s<known> ?? " # TODO known: {%s<known>}" !! '';

    # --once: does the shape run at all, cleanly, at a small size
    if $ONCE {
        my ($secs, $v, $out, $err) = run-once(file-for(%s, 20), 60);
        my $warn = $err.lines.first(*.trim.chars);
        if $v ne 'ok' || $warn.defined {
            say "not ok $test - $label: {$v ne 'ok' ?? $v !! "warns: {$warn.trim.substr(0, 160)}"}";
            @bad.push: $label;
        }
        else {
            say "ok $test - $label runs";
        }
        next;
    }

    # grow n until a run is long enough to time, shrink it while too slow; then
    # the short size's best of three
    my $n = %s<n>;
    my ($short, $v, $wall) = measure(%s, $n, 1, 60);
    while $v eq 'ok' && $short < %FLOOR-MS{%s<domain>} && $n < %s<n> * $GROW-CAP {
        $n *= 2;
        ($short, $v, $wall) = measure(%s, $n, 1, 60);
    }
    while $v eq 'ok' && $short > $CEIL-MS && $n > max(8, %s<n> div $GROW-CAP) {
        $n div= 2;
        ($short, $v, $wall) = measure(%s, $n, 1, 60);
    }
    ($short, $v, $wall) = measure(%s, $n, 3, 60) if $v eq 'ok';
    if $v ne 'ok' {
        say "not ok $test - $label: the program at n={fmt-n($n)} did not run: $v$todo";
        (%s<known> ?? @known-bad !! @bad).push: $label;
        next;
    }

    # the long run: linear is about $K times the short one, and a run that has
    # taken half as long again as the most that could pass is stopped
    my $long-n  = $n * $K;
    my $timeout = 2 + $wall * $MAX-RATIO * 1.5;
    my ($long, $lv) = measure(%s, $long-n, %s<known> ?? 1 !! 3, $timeout, :enough($short * $MAX-RATIO));
    my $ratio = $long / $short;
    my $what = "{fmt-n($n)} -> {fmt-n($long-n)}: {$short.fmt('%.0f')} -> {$long == Inf ?? '∞' !! $long.fmt('%.0f')} ms";
    note "# $label  n=$what  x{$ratio == Inf ?? '∞' !! $ratio.fmt('%.1f')}" if $VERBOSE;

    my $superlinear;
    my $verdict;
    if $lv eq 'timeout' {
        $superlinear = True;
        $verdict = "timed out at n={fmt-n($long-n)} after {$timeout.fmt('%.0f')} s (n={fmt-n($n)} took {$short.fmt('%.0f')} ms)";
    }
    elsif $lv ne 'ok' {
        say "not ok $test - $label: the program at n={fmt-n($long-n)} did not run: $lv$todo";
        (%s<known> ?? @known-bad !! @bad).push: $label;
        next;
    }
    else {
        $superlinear = $ratio > $MAX-RATIO && $long >= $JUDGE-MS;
        $verdict = "x{$ratio.fmt('%.1f')} for x$K the size ($what)";
    }

    if $superlinear {
        say "not ok $test - $label superlinear: $verdict$todo";
        (%s<known> ?? @known-bad !! @bad).push: $label;
    }
    else {
        say "ok $test - $label linear: $verdict$todo";
        @known-fixed.push: $label if %s<known>;
    }
}

if $ONCE {
    say "# {+@run} shapes run once at a small size: {@run - @bad} cleanly, {+@bad} not";
    say "# FAILED: {@bad.join(', ')}" if @bad;
    exit(@bad ?? 1 !! 0);
}
my $lin = @run - @bad - @known-bad;
say "# {+@run} shapes: $lin linear, {+@bad} superlinear or failing, {+@known-bad} known superlinear (TODO), {(now - $START).fmt('%.0f')} s";
say "# known superlinear and now LINEAR — drop the `known` mark: {@known-fixed.join(', ')}" if @known-fixed;
say "# FAILED: {@bad.join(', ')}" if @bad;
exit(@bad ?? 1 !! 0);
