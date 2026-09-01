# The Text::CodeProcessing install (docs/dev/ecosystem/MODULE-FINDINGS.md,
# 2026-09-01). The dist stopped at `Undefined routine 'nqp::getcomp'`; behind
# that op were five more faults, none of them about nqp. Every check below
# section 1 is oracle-verified and runs under Rakudo too; section 1 says why it
# is the exception.
#
#   1. nqp::getcomp('Raku') + REPL.new + .repl-eval — the sandbox idiom
#      (Jupyter::Kernel's, copied into every weaver): a scope that PERSISTS
#      across evals, sees the caller's $*OUT, and reports a bad line.
#   2. `$<a>=( … )` is a capture SCOPE ($<a><b>, no top-level $<b>);
#      `$<a>=[ … ]` only groups, so its contents stay at the top level.
#   3. a declared `my regex R {…}` works as the pattern of .subst/.split —
#      it arrives as the Callable `&R`, which those methods did not recognise.
#   4. `<NAME>` resolves inside .subst's pattern, as it does under `~~`.
#   5. a capture's own tree survives into .subst's replacement block.
#   6. Hash(%a, %b, %c) merges all three; a Hash element in a list contributes
#      its pairs rather than stringifying into a key.
#   7. the two compile diagnostics a document weaver publishes verbatim.
#
# Contract: exit 0 + last line PASS.
use nqp;

my @fail;
sub check($got, $want, $what) { @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want }

# ---- 1. the REPL sandbox ------------------------------------------------
# The idiom verbatim: the compiler by name, a REPL over it, and each line run
# with $*CTXSAVE set so the compiler can hand the eval'd scope back through
# $*MAIN_CTX, which is then offered as :outer_ctx next time.
#
# THIS SECTION IS rakupp-ONLY, and deliberately: rakupp keeps the session on
# the REPL object and ignores :outer_ctx, so the same three lines that persist
# a scope here persist nothing under Rakudo unless its own context objects are
# threaded exactly right. The cross-engine oracle for this surface is the
# distribution that drives it — Text::CodeProcessing's own suite, which the
# install gate runs. Everything below section 1 runs under both engines.
my $rakupp = $*RAKU.compiler.name eq 'Raku++';
my $compiler := nqp::getcomp("Raku") || nqp::getcomp('perl6');
check(!($compiler =:= Any), True, 'nqp::getcomp names the running compiler');

# (constructed only here: Rakudo's REPL greets the terminal on construction)
my $repl = $rakupp ?? REPL.new($compiler, {}) !! Nil;
my $save-ctx;
my $exception;
sub run-line(Str $code) {
    my $*CTXSAVE = $repl;
    my $*MAIN_CTX;
    $exception = Nil;
    my $result = $repl.repl-eval($code, $exception, :outer_ctx($save-ctx), :interactive(1));
    $save-ctx = $*MAIN_CTX if $*MAIN_CTX;
    $result;
}

if $rakupp {
    check(run-line('my $answer = 42').Int, 42, 'repl-eval returns the line value');
    check(run-line('$answer ** 2').Int, 1764, 'the scope PERSISTS across evals');
    check($exception.defined, False, 'a good line reports no exception');

    # the caller's $*OUT is the one a line prints to (a weaver captures a
    # chunk's output by wrapping the call in exactly this)
    my $out;
    {
        my $*OUT = $*OUT but role { method print (*@args) { $out ~= @args } };
        run-line('say "captured"');
    }
    check($out, "captured\n", 'output goes to the CALLER\'s $*OUT');

    # a line that does not compile: no value back, and the exception reported
    my $bad = run-line('$answer ** ');
    check($bad.defined, False, 'a failed line answers no value');
    check(($exception andthen $exception.Str.contains('Missing required term after infix')), True,
          'a failed line reports through the second argument');
}

# ---- 2. a paren capture is a capture SCOPE ------------------------------
'x' ~~ / $<a>=( $<b>=('x') ) /;
check($/.hash.keys.sort.join(','), 'a', 'names inside $<a>=( … ) leave the top level');
check($<a><b>.Str, 'x', '…and are reachable through the capture');
'xy' ~~ / $<a>=[ $<b>=('x') ] $<c>=('y') /;
check($/.hash.keys.sort.join(','), 'a,b,c', '$<a>=[ … ] groups without scoping');

# ---- 3./4./5. a declared regex as a pattern, subrules and all ------------
my regex pair-of { $<key>=(\w+) '=' $<val>=(\w+) }
my regex header  { '{' $<lang>=(\w+) [ ',' \h* $<params>=(<pair-of>) ]? '}' }

check(('a=1' ~~ &pair-of).so, True, '~~ &R matches (it always did)');
check('[a=1]'.subst(&pair-of, 'X'), '[X]', '.subst takes a declared regex as its pattern');
check('x a=1 y'.split(&pair-of).join('|'), 'x | y', '.split does too');

# the pattern names another regex, and the replacement block reads the tree
my $seen;
my $woven = '{raku, eval=FALSE}'.subst(&header, -> $m {
    $seen = ($m<lang>.Str, $m<params><pair-of><key>.Str, $m<params><pair-of><val>.Str).join('/');
    'HIT'
});
check($woven, 'HIT', '<NAME> resolves in the pattern .subst compiles');
check($seen, 'raku/eval/FALSE', 'the replacement block reads the capture tree');

# ---- 6. Hash( … ) is a merge -------------------------------------------
my %a = :x(1), :y(2);
my %b = :y(20), :z(30);
my %c = :w(40);
check(Hash(%a, %b, %c), {:w(40), :x(1), :y(20), :z(30)}, 'Hash( … ) merges every argument');
check((%a, %b).Hash, {:x(1), :y(20), :z(30)}, 'a Hash in a list contributes its pairs');

# ---- 7. the two diagnostics --------------------------------------------
# (a weaver puts these INTO the document it produces, so the wording is pinned)
try EVAL 'my $x = 42 *';
check(($! andthen $!.Str.contains('Missing required term after infix')), True,
      'an infix with no operand is named');
try EVAL "\$(\n1\n2\n)";
check(($! andthen $!.Str.contains('Two terms in a row across lines')), True,
      'two statements with no separator are named');

if @fail { note "FAILED:\n  " ~ @fail.join("\n  "); say 'FAIL'; exit 1 }
say 'PASS';
