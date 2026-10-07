# Issue #35: `rakupp install zef` then `zef --version` died "Could not find
# Zef". That much had been fixed since, but USING the installed zef still
# failed, and behind it sat six general engine bugs, each hiding the next:
#
#   * `for %h -> $p` handed out COPIED Pairs. Rakudo's hold the entry's own
#     container, so `%h{$p.key} = …` in the body shows through `$p.value`.
#     zef's config expansion substitutes `$*HOME`, `$*TMPDIR`, `$*PID` and
#     `{time}` one after another, re-reading `$node.value` each time; only
#     the last survived, so zef made literal `$*HOME/` and `$*TMPDIR/`
#     directories wherever it ran.
#   * `&*EXIT` was undeclared. It is `exit` until rebound, and zef's CLI
#     ends every error with `&*EXIT(1)`; without it, every error message was
#     replaced by "Undefined routine '*EXIT'".
#   * assigning to a COERCION-typed attribute (`has Str() $.uri is rw;
#     $c.uri = $path.IO`) refused the value instead of coercing it, as `.new`
#     already did.
#   * CompUnit::Repository::Staging did not exist. zef 1.x installs through
#     one: stage, test against the staged copy, `.deploy` into the target.
#   * a lazy `.map` (over a gather) kept the Slips its block returned
#     instead of flattening them: zef's `.plugins.map(*.Slip)`.
#   * `my $x := <value>; for $x { … }` ran once: a `$` name BOUND to a list
#     has no item container, so it iterates (zef's `for $all-plugins`).
#   * and run-script (the installed `zef` wrapper) ran the program inside
#     the wrapper's MAIN frame with an @*ARGS of its own, so the one
#     Zef::CLI's mainline reorders (options first) was not the one MAIN
#     dispatched on: `zef list --installed` listed what was AVAILABLE.
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $desc) {
    @fail.push("$desc: got «{$got.raku}», wanted «{$want.raku}»") unless $got eqv $want;
}

# -- a hash's iteration Pairs hold its containers -------------------------------
{
    my %h = a => 1;
    my @seen;
    for %h -> $p { %h{$p.key} = 2; @seen.push: $p.value }
    check(@seen, [2], 'a store through the hash shows through the iteration Pair');
}
{
    my %h = a => 1;
    for %h -> $p { $p.value = 6 }
    check(%h<a>, 6, '.value = … through the iteration Pair writes the hash');
}
{
    my %h = a => 1;
    my @seen;
    for %h.pairs { %h<a> = 3; @seen.push: .value }
    check(@seen, [3], '…and so does a `for %h.pairs` topic');
}
{
    # zef's Zef::Config::parse-file, in miniature
    my %config = StoreDir => '$*HOME/.zef/store', TempDir => '$*TMPDIR/.zef.{time}';
    for %config -> $node {
        %config{$node.key} = $node.value.subst('$*HOME', '/H', :g);
        %config{$node.key} = $node.value.subst('$*TMPDIR', '/T', :g);
        %config{$node.key} = $node.value.subst('{time}', '42', :g);
    }
    check(%config<StoreDir>, '/H/.zef/store', 'successive substitutions through $node.value all apply');
    check(%config<TempDir>, '/T/.zef.42', '…on every entry');
}
{
    # a Map's Pairs stay read-only
    my $m = Map.new((a => 1));
    my $died = False;
    for $m.pairs -> $p { try { $p.value = 2; CATCH { default { $died = True } } } }
    check($died, True, "a Map's iteration Pair still refuses a write");
}

# -- &*EXIT ----------------------------------------------------------------------
check(&*EXIT.name, 'exit', '&*EXIT is `exit` until rebound');
check(&*EXIT ~~ Callable, True, '&*EXIT is callable');
{
    sub intercepted { my &*EXIT = sub ($c) { "caught $c" }; &*EXIT(2) }
    check(intercepted(), 'caught 2', 'a caller rebinding &*EXIT intercepts the call');
}
{
    my $p = run $*EXECUTABLE, '-e', 'note "EXIT35"; &*EXIT(3); say "not reached"', :out, :err;
    my $out = $p.out.slurp(:close);
    my $err = $p.err.slurp(:close);
    check($p.exitcode, 3, '&*EXIT(3) exits the process with 3');
    check($out, '', '…before anything after it runs');
    check($err.contains('EXIT35'), True, '…and the program did run up to it');
}

# -- assignment to a coercion-typed attribute ------------------------------------
{
    class Cand35 { has Str() $.uri is rw; has Int() $.n is rw; method set($v) { $!uri = $v } }
    my $c = Cand35.new;
    $c.uri = '/b'.IO;
    check($c.uri.WHAT, Str, 'an IO::Path assigned to a Str() attribute is a Str');
    check($c.uri, '/b', '…holding the path');
    $c.n = '42';
    check($c.n, 42, 'a Str assigned to an Int() attribute is the Int');
    $c.set('/c'.IO);
    check($c.uri, '/c', '`$!uri = …` inside a method coerces too');
}
{
    class Lim35 { has Int() $.n is rw where * > 0 = 1 }
    my $l = Lim35.new;
    $l.n = '5';
    check($l.n, 5, 'the coercion runs before a `where`');
    my $refused = False;
    try { $l.n = '-3'; CATCH { default { $refused = True } } }
    check($refused, True, '…which still refuses the coerced value it rejects');
    check($l.n, 5, '…and leaves the old value');
}

# -- a lazy .map flattens the Slips its block returns ----------------------------
check((gather { take [1, 2]; take [3] }).map(*.Slip).List, (1, 2, 3), 'a gather .map(*.Slip) flattens');
check((gather { take 1; take 2 }).map({ slip $_, $_ }).List, (1, 1, 2, 2), '…as does a block answering slip(…)');
check((gather { take 1; take 2; take 3 }).map({ $_ == 2 ?? Empty !! $_ }).List, (1, 3), 'Empty adds nothing');
check((gather { take 1; take 2 }).map({ slip() }).List, (), '…even for every element');
check((^Inf).map({ slip $_, -$_ }).head(5).List, (0, 0, 1, -1, 2), 'an endless source flattens lazily');
check((gather { take [1, 2]; take [3] }).map({ $_ }).List, ([1, 2], [3]), 'an Array the block answers stays one element');

# -- `for` over a `$` name bound to a value ---------------------------------------
{
    sub groups35 { my @p = [1, 2], [3]; @p }
    my $all := groups35();
    my $n = 0;
    for $all -> @g { $n++ }
    check($n, 2, '`my $x := <Array>; for $x` iterates its elements');
    $n = 0;
    $n++ for $all;
    check($n, 2, '…in the statement-modifier form too');
    my $l := (1, 2, 3);
    $n = 0;
    for $l { $n++ }
    check($n, 3, '…and a bound List');
    my $i := $(1, 2);
    $n = 0;
    for $i { $n++ }
    check($n, 1, 'an itemized value bound is still one item');
    my $s = (1, 2);
    $n = 0;
    for $s { $n++ }
    check($n, 1, 'an ASSIGNED `$` stays one item');
}

# -- CompUnit::Repository::Staging ------------------------------------------------
{
    my $root = $*TMPDIR.child("rakupp-issue35-{$*PID}-{now.Num}");
    my $dist-dir = $root.child('dist').mkdir;
    $dist-dir.child('lib').mkdir;
    $dist-dir.child('lib/Staged35.rakumod').spurt: "unit module Staged35; our sub hi \{ 'staged-35' }\n";
    $dist-dir.child('META6.json').spurt:
        '{"name":"Staged35","version":"0.1","auth":"zef:test","provides":{"Staged35":"lib/Staged35.rakumod"}}';
    my $target = CompUnit::Repository::Installation.new(:prefix($root.child('target').absolute), :name<t35>);
    my $staging = CompUnit::Repository::Staging.new(
        :prefix($root.child('stage').absolute), :name<t35>, :next-repo($target));
    check($staging.^name, 'CompUnit::Repository::Staging', 'Staging.new makes a staging repository');
    check($staging ~~ CompUnit::Repository::Installation, True, '…which is an Installation');
    check($staging.short-id, 'staging', '…whose short-id is "staging"');
    $staging.install(Distribution::Path.new($dist-dir));
    check($root.child('stage/dist').dir.elems, 1, 'install writes the dist into the staging prefix');
    check($root.child('target/dist').e, False, '…and not into the target');
    $root.child('stage/repo.lock').spurt('');
    $staging.remove-artifacts;
    check($root.child('stage/repo.lock').e, False, 'remove-artifacts drops the lock files');
    $staging.deploy;
    check($root.child('target/dist').dir.elems, 1, 'deploy copies the dist into the target');
    my $p = run $*EXECUTABLE, '-I', "inst#{$root.child('target').absolute}",
                '-e', 'use Staged35; print Staged35::hi()', :out, :err;
    check($p.out.slurp(:close), 'staged-35', '…where `use` finds it');
    $p.err.slurp(:close);
    $staging.self-destruct;
    check($root.child('stage').e, False, 'self-destruct removes the staging prefix');
    run 'rm', '-rf', $root.absolute if $root.absolute.contains('rakupp-issue35-');
}

# -- run-script: the program's @*ARGS is the one MAIN dispatches on --------------
{
    my $root = $*TMPDIR.child("rakupp-issue35rs-{$*PID}-{now.Num}");
    my $dist-dir = $root.child('dist').mkdir;
    $dist-dir.child('lib').mkdir;
    $dist-dir.child('bin').mkdir;
    $dist-dir.child('lib/ArgOrder35.rakumod').spurt: q:to/END/;
        unit module ArgOrder35;
        sub reorder {
            for @*ARGS -> $arg {
                state @positional; state @named;
                LAST { @*ARGS = flat @named, @positional; }
                $arg.starts-with('-') ?? @named.append($arg) !! @positional.append($arg);
            }
        }
        reorder();
        proto MAIN(|) is export { {*} }
        multi sub MAIN('list', Bool :i(:$installed), *@at) { print "installed=$installed.raku() at=@at.raku()" }
        END
    $dist-dir.child('bin/argorder35').spurt: "use ArgOrder35;\n";
    $dist-dir.child('META6.json').spurt:
        '{"name":"ArgOrder35","version":"0.1","auth":"zef:test","provides":{"ArgOrder35":"lib/ArgOrder35.rakumod"},'
        ~ '"files":{"bin/argorder35":"bin/argorder35"}}';
    my $repo = CompUnit::Repository::Installation.new(:prefix($root.child('repo').absolute), :name<rs35>);
    $repo.install(Distribution::Path.new($dist-dir));
    my $wrapper = 'sub MAIN(:$name is copy, :$auth, :$ver, *@, *%) { CompUnit::RepositoryRegistry.run-script("argorder35", :dist-name<ArgOrder35>, :$name, :$auth, :$ver) }';
    my $p = run $*EXECUTABLE, '-I', "inst#{$root.child('repo').absolute}", '-e', $wrapper, 'list', '--installed', :out, :err;
    my $err = $p.err.slurp(:close);
    check($p.out.slurp(:close), 'installed=Bool::True at=[]',
          'a module that moves options first is heard by MAIN under run-script');
    run 'rm', '-rf', $root.absolute if $root.absolute.contains('rakupp-issue35rs-');
}

# -- zef's own suite, from a checkout (`zef install .` runs it) -----------------
# `my $b := @a` binds the Array itself: `for $b` iterates it, and two names
# bound to one Array are =:= (distinct Arrays are not)
{
    my @a = 1, 2, 3; my %h = x => 1, y => 2;
    my $b := @a; my $k := @a; my $g := %h;
    my $n = 0;
    for $b { $n++ }
    check($n, 3, '`my $b := @a; for $b` iterates the Array');
    $n = 0;
    $n++ for $b;
    check($n, 3, '…in the statement-modifier form too');
    $n = 0;
    for $g { $n++ }
    check($n, 2, '…and `$g := %h` iterates the Hash');
    check($b =:= $k, True, 'two names bound to one Array are =:=');
    check($b =:= @a, True, '…and each is =:= the Array');
    my @c = 1, 2, 3; my $d := @c;
    check($b =:= $d, False, 'names bound to different Arrays are not');
    sub via-param(@x) { my $y := @x; my $m = 0; for $y { $m++ }; $m }
    check(via-param(@a), 3, 'a bind to an @-parameter iterates too');
}
# a curried subscript reads a Pair on its one key, and an object through AT-KEY
# (zef's `.grep(*.<requires>)` over the pairs of a `depends` hash)
{
    my %h = requires => [3];
    check(%h.grep(*.<requires>).map(*.<requires>).List, ([3],), '`*.<k>` reads a hash entry Pair');
    check((r => 1, s => 2).map(*<r>).List, (1, Nil), '`*<k>` is Nil on a Pair with another key');
    check((r => 1, s => 2).map(*<r s>).List, ((1, Nil), (Nil, 2)), '…and a curried slice reads each key');
    class AtKey35 does Associative { method AT-KEY($k) { "at-$k" } }
    check((AtKey35.new,).map(*<z>).List, ('at-z',), 'an object answers a curried subscript through AT-KEY');
    check((r => 7).AT-KEY('r'), 7, 'Pair.AT-KEY');
    check((r => 7).AT-KEY('q'), Nil, '…Nil for another key');
    check((r => 7).EXISTS-KEY('r'), True, 'Pair.EXISTS-KEY');
    check((r => 7).EXISTS-KEY('q'), False, '…False for another key');
}
# CompUnit::Repository requires need, loaded and id; `load` has a default
{
    my $ok = True;
    try { EVAL 'class :: does CompUnit::Repository { method need { }; method loaded { }; method id { } }'; CATCH { default { $ok = False } } }
    check($ok, True, 'a repository class needs no `load` of its own');
    my $refused = False;
    try { EVAL 'class :: does CompUnit::Repository { method need { }; method loaded { } }'; CATCH { default { $refused = True } } }
    check($refused, True, '…but still must supply `id`');
}
# .resolve drops `.` segments, even past a directory that does not exist
check(IO::Path.new('.', :CWD('/nope35/q')).resolve.Str, '/nope35/q', '`.` under a missing CWD resolves to the CWD');
check('/nope35/a/./b'.IO.resolve.Str, '/nope35/a/b', 'a `.` mid-path goes');
check('/nope35/a/../b'.IO.resolve.Str, '/nope35/a/../b', '`..` past a missing directory stays');

if @fail { .say for @fail; say 'FAIL' } else { say 'PASS' }
