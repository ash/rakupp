# Regression: the antononcube batch, round two (2026-08-28) — the second
# blocker ring: sibling-role grammars, membership identity, user metamethods,
# rule args, and CStruct field freshness. Expectations read off RAKUDO first.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) {
    @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want
}

# ---- membership is an identity question --------------------------------------
check(('123' (elem) [123]),        False, 'Str is not elem of the Int');
check((123 (elem) [123]),          True,  'the Int itself is');
check((1.0 (elem) [1]),            False, 'Rat is not the Int');
check((<42> (elem) [42]),          False, 'an allomorph is neither half');
check((42 (elem) {a => 42}),       False, 'hash VALUES are not elements');
check(('a' (elem) {a => 42}),      True,  'hash KEYS are');
check(([123] (cont) '123'),        False, '(cont) mirrors it');
# …while a quanthash subscript keys the same way its constructor did
my $s = set(<a b>);
check($s<a>,        True,  'set subscript');
check(($s<a>:exists), True, 'set :exists');
check($s.pick.WHAT.^name, 'Str', '.pick answers the ELEMENT, not the storage key');
check(set(1,2).pick.WHAT.^name, 'Int', '…of the element type that went in');

# ---- a SECOND sibling role's grammar rules exist ------------------------------
{
    role RuleA { token hello { 'ha' } }
    role RuleB { proto token greet {*}; token greet:sym<x> { :i 'set' } }
    grammar TwoRoles does RuleA does RuleB { }
    check((TwoRoles.parse('ha',  rule => 'hello') // Nil).Str, 'ha',  'first role rules');
    check((TwoRoles.parse('set', rule => 'greet') // Nil).Str, 'set', 'second role rules survive composition');
    # …and a composed role's OWN sub-role rides along
    role Deep { token deep-word { 'dw' } }
    role Carrier does Deep { token carrier-word { 'cw' } }
    role Other { token other-word { 'ow' } }
    grammar Chain does Other does Carrier { }
    check((Chain.parse('dw', rule => 'deep-word') // Nil).Str, 'dw', 'role-of-role rules survive too');
}

# ---- `.parse(:rule, args => (…))` binds the start rule's parameters -----------
{
    grammar Gated {
        token TOP($*ext) { <spec> }
        token spec { 'a' | 'b' <?{ $*ext }> }
    }
    check((Gated.parse('b', args => (True,))  // Nil).defined, True,  'args bind a dynamic rule param');
    check((Gated.parse('b', args => (False,)) // Nil).defined, False, '…as a real value, not a string');
}

# ---- `<name=[…]>` is a named capture of an inline class -----------------------
{
    grammar Offs { token num-offset { <sign=[+-]> <hour=[\d]>**2 } }
    my $m = Offs.parse('-08', rule => 'num-offset');
    check($m.defined, True, 'a named inline class matches');
    check((~$m<sign>), '-', '…and captures under the alias');
}

# ---- a user `method ^parameterize` owns `T[…]` --------------------------------
{
    class Parametric { method ^parameterize(Mu:U \obj, **@pos) { "custom({@pos.join(',')})" } }
    class Uses is Parametric { }
    check(Uses[7, 8], 'custom(7,8)', 'T[…] runs the user metamethod');
    check(Uses.^parameterize(9), 'custom(9)', '…and .^name calls dispatch to it');
}
# `.^set_name` renames for real, and the same parameterization is ONE type
{
    class Blank { }
    my \renamed = Blank.^mixin().^set_name('Renamed');
    check(renamed.^name, 'Renamed', '^set_name takes effect');
    role Tag[$x] { }
    check((Tag[42].^name eq Tag[42].^name), True, 'one pun per parameterization');
    class Host { }
    my $tagged = Host but Tag[42];
    check(($tagged ~~ Tag[42]), True, '…so a later smartmatch agrees');
}

# ---- the :D/:U smiley is enforced on an ordinary bind -------------------------
{
    sub wants-instance(Int:D $x) { $x }
    sub wants-type(Int:U $x)     { $x.^name }
    check((try { wants-instance(Int); 'bound' }) // 'threw', 'threw', ':D refuses a type object');
    check(wants-instance(5), 5, '…and takes an instance');
    check((try { wants-type(5); 'bound' }) // 'threw', 'threw', ':U refuses an instance');
    # an explicit role parameterization type-checks the same way (spelled through
    # the metamodel so BOTH engines decide it at run time — the literal NeedsD[Int]
    # is a compile-time error under Rakudo, which `try` cannot catch)
    role NeedsD[Any:D $v] { }
    check((try { EVAL 'NeedsD[Int]'; 'bound' }) // 'threw', 'threw', 'a role arg fails :D too');
}

# ---- a CStruct's fields are read fresh from native memory ---------------------
# (self-contained: compile a one-function C library into the scratch area)
{
    my $dir = $*TMPDIR.add("rk-cstruct-{$*PID}");
    $dir.mkdir;
    my $c = $dir.add("lib.c");
    $c.spurt(q:to/END/);
        typedef struct CS { double re; double im; } CS;
        int fill(CS *out, double a, double b) { out->re = a; out->im = b; return 0; }
        END
    my $dylib = $dir.add($*DISTRO.is-win ?? "crw.dll" !! ($*KERNEL.name eq 'darwin' ?? "libcrw.dylib" !! "libcrw.so"));
    my $cc = run 'cc', '-shared', '-o', $dylib.absolute, $c.absolute, :out, :err;
    $cc.out.slurp(:close); $cc.err.slurp(:close);
    if $cc.exitcode == 0 {
        use NativeCall;
        my $code = Q:to/END/.subst('LIBPATH', $dylib.absolute);
            use NativeCall;
            class CS is repr('CStruct') {
                has num64 $.re; has num64 $.im;
                method value() { $!re + $!im * i }
            }
            sub fill(CS is rw, num64, num64 --> int32) is native('LIBPATH') {*}
            my $s = CS.new();
            fill($s, 3e0, 4e0);
            print $s.value;
            END
        my $p = run $*EXECUTABLE, '-e', $code, :out, :err;
        my $out = $p.out.slurp(:close); $p.err.slurp(:close);
        check($out, '3+4i', 'a method reading $!fields sees what C wrote');
    }
    unlink $c; unlink $dylib if $dylib.e; rmdir $dir;
}

say @fail ?? "FAILED:\n" ~ @fail.join("\n") !! 'PASS';
exit @fail ?? 1 !! 0;
