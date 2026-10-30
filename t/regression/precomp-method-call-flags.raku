# Regression: a module loaded from the parse cache keeps the flags the parser
# set on its calls and operators.
#
# The AST cache's visitors wrote a MethodCall's `maybe`, `bang`, `mutate`,
# `hyper` and `meta`, and nothing of the flags added since: `allMode` (`.+`,
# `.*`), `lazyTail` (a returned map is a lazy Seq, c0691dff), `curryClosed`
# (`(( … ))` finishes a WhateverCode — on Binary and WhateverExpr too) and
# `fatalExempt` (`so f()` under `use fatal` tests the Failure — on Call too).
# The first run parsed the module and was right; every run after loaded it
# from the cache and was not.
#
# Found in issue #60: with --precomp-modules=on, `rakupp install
# FunctionalParsers` failed t/06-chain-left-and-right, "Too few positionals
# passed". 00-sanity cached the module; 06 loaded it, its `apply` map ran every
# parse where `.head` reads one, and the third parse's reduce died.
#
# Contract: exit 0 + last line PASS.
my @fail;
my $dir = $*TMPDIR.add("precomp-method-call-flags-{$*PID}");
$dir.mkdir;
LEAVE { try { my sub rm($d) { for $d.dir { $_.d ?? rm($_) !! .unlink }; $d.rmdir }; rm($dir) } }
my $lib   = $dir.add('lib');   $lib.mkdir;
my $cache = $dir.add('cache'); $cache.mkdir;

$lib.add('MCFlags.rakumod').spurt(q:to/MOD/);
    unit module MCFlags;
    class A { method m { 'A' } }
    class B is A { method m { 'B' } }
    sub every-m is export { B.new.+m.join(',') }
    sub parses is export { (1, 2, 3).map({ $_ == 3 ?? die('read past the head') !! $_ * 10 }) }
    sub apply(&f) is export { -> @x { @x.map({ $_ == 3 ?? die('read past the head') !! f($_) }) } }
    sub flip-abc is export { ((*.flip)).assuming('abc') }
    sub cat-abc is export { ((* ~ 'x')).assuming('abc') }
    sub star-name is export { ((*)).^name }
    sub nope { fail 'nope' }
    class K { method nope { fail 'nope' } }
    sub so-call is export { use fatal; so nope() }
    sub so-meth is export { use fatal; so K.nope }
    MOD

my @checks =
    'every-m()'                        => 'B,A',         # MethodCall.allMode
    'parses().head'                    => '10',          # MethodCall.lazyTail, a sub's value
    'apply(* * 10)((1, 2, 3)).head'    => '10',          # …and a closure's (FunctionalParsers' apply)
    'flip-abc()()'                     => 'cba',         # MethodCall.curryClosed
    'cat-abc()()'                      => 'abcx',        # Binary.curryClosed
    'star-name()'                      => 'Whatever',    # WhateverExpr.curryClosed
    'so-call()'                        => 'False',       # Call.fatalExempt
    'so-meth()'                        => 'False';       # MethodCall.fatalExempt
my $prog = "use MCFlags;\n" ~ @checks.map({ "say try \{ {.key} \} // \"died: \{\$!.message\}\";" }).join("\n");

my %env = %*ENV;
%env<RAKUPP_PRECOMP_DIR>     = $cache.Str;
%env<RAKUPP_PRECOMP_MODULES> = '1';
%env<RAKUPP_CONFIG>          = $dir.add('rakupp.config').Str;   # never the real one
%env<RAKUPP_NO_PRECOMP>:delete;

sub entries() {
    my sub walk($d) { $d.dir.map({ .d ?? walk($_).Slip !! $_ }) }
    +walk($cache).grep(*.Str.ends-with('.ast'))
}

# run 1 parses the module and writes its entry; runs 2 and 3 read it
for 1..3 -> $run {
    my $p = run($*EXECUTABLE, '-I', $lib.Str, '-e', $prog, :env(%env), :out, :err);
    my @got = $p.out.slurp(:close).lines;
    my $err = $p.err.slurp(:close);
    @fail.push("run $run: exit {$p.exitcode} — {$err.lines.head // ''}") if $p.exitcode;
    for @checks.kv -> $i, $c {
        @fail.push("run $run: {$c.key} is {(@got[$i] // '(nothing)').raku}, want {$c.value.raku}")
            unless (@got[$i] // '') eq $c.value;
    }
    # without an entry, runs 2 and 3 parsed afresh and proved nothing
    @fail.push("run 1 left no cache entry in $cache")
        if $run == 1 && $*RAKU.compiler.name eq 'Raku++' && !entries();
}

if @fail {
    note "FAIL: $_" for @fail;
    say "FAIL: $_" for @fail;
    say "FAIL ({+@fail})";
    exit 1;
}
say "PASS";
