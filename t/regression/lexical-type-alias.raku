# Regression: a type NAME means what it means in the scope that wrote it.
# Type checks look a name up in the class table, which is keyed by short name,
# so once an unrelated `class Bar` existed anywhere in the program, a lexical
# alias of the same name — an import (`sub EXPORT { 'Bar' => Foo }`, roast's
# packages/RT125715) or a `my constant Bar = Foo` — lost to it: `my Bar $v =
# Bar.new` died "expected Bar but got Foo", and so did a `Bar $p` parameter and
# a `multi m(Bar $x)` candidate. The attribute form (`has Bar $.bar`,
# S11-modules/export.t) was already read in the class's declaring scope; a
# variable is now read in the scope that declared it, and a signature in the
# one that wrote it — each only once the global reading has refused.
#
# Every expectation below was checked against Rakudo 2026.09.

use lib $?FILE.IO.parent.add('lib').Str;

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}
sub lives(&code, $desc) {
    my $ok = try { code(); True };
    if $ok { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$!.message}" }
}
sub dies(&code, $type, $desc) {
    my $ok = try { code(); True };
    if !$ok && $!.^name eq $type { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — " ~ ($ok ?? 'lived' !! $!.^name) }
}

# the unrelated class that owns the name globally
{ class Bar { method x { 1 } } }

{
    use RakuppAliasExport;
    ck(Bar.^name, 'RakuppAliasTarget', 'the import makes Bar an alias here');

    lives({ my Bar $v = Bar.new }, 'a typed variable takes an instance');
    lives({ my Bar $v; $v = Bar.new }, '…assigned after its declaration');
    lives({ my Bar $v = Bar.new; $v = Bar.new }, '…and assigned again');
    lives({ my Bar:D $v = Bar.new }, '…with a :D smiley');
    lives({ my Bar $v = Bar }, '…and takes the type object');
    dies({ my Bar $v = 42 }, 'X::TypeCheck::Assignment', '…still refusing what it is not');

    sub f(Bar $p) { 'param' }
    sub fd(Bar:D $p) { 'param:D' }
    sub fu(Bar:U $p) { 'param:U' }
    ck(f(Bar.new), 'param', 'a typed parameter takes an instance');
    ck(f(Bar), 'param', '…and the type object');
    ck(fd(Bar.new), 'param:D', '…with a :D smiley');
    ck(fu(Bar), 'param:U', '…with a :U smiley');
    my $n = 42;
    dies({ f($n) }, 'X::TypeCheck::Binding::Parameter', '…still refusing what it is not');
    ck((-> Bar $p { 'pointy' })(Bar.new), 'pointy', 'a pointy block parameter');

    my class Q { method m(Bar $p) { 'method' } }
    ck(Q.m(Bar.new), 'method', 'a method parameter');

    multi m(Bar $x) { 'bar' }
    multi m(Int $x) { 'int' }
    ck(m(Bar.new), 'bar', 'a multi candidate takes an instance');
    ck(m(42), 'int', '…and its sibling still gets the Int');

    my class Baz { has Bar $.bar }
    ck(Baz.new(bar => Bar.new).bar.^name, 'RakuppAliasTarget', 'an attribute (already right)');
}

# the same with a constant naming another class
{ class Qux { } }
class QuxTarget { }
{
    my constant Qux = QuxTarget;
    lives({ my Qux $v = QuxTarget.new }, 'a constant alias: typed variable');
    sub g(Qux $p) { 'const param' }
    ck(g(QuxTarget.new), 'const param', 'a constant alias: typed parameter');
}

# …and a multi candidate in a program whose only alias is a constant (its own
# process, so the import above cannot be what makes dispatch look)
my $p = run $*EXECUTABLE, '-e', q:to/END/, :out, :err;
    { class Quux { } }
    class QuuxTarget { }
    {
        my constant Quux = QuuxTarget;
        multi h(Quux $x) { 'const multi' }
        multi h(Int $x) { 'int' }
        print h(QuuxTarget.new), ' ', h(1);
    }
    END
$p.err.slurp(:close);
ck($p.out.slurp(:close), 'const multi int', 'a constant alias: multi candidate');

die "$fails check(s) failed" if $fails;
say 'PASS';
