# Regression: an INDIRECT method call — `$obj."$name"()`, `$obj.$callable`,
# `$obj.&sub`, `self!"$name"()` — was outside the native subset, so every
# program containing one fell back to bundling the interpreter (issue #45):
#
#   note: an indirect method call (."$name"()) — not yet natively compiled;
#         bundling the whole program with the interpreter instead.
#
# Codegen now evaluates the name expression and hands it to rtIndirectMethod,
# which CALLS a Code (or a type object — `$v.$ct` with `my $ct = Rat` is the
# coercion Rat($v)) with the invocant first, and dispatches anything else by
# its stringified name.
#
# The private form forced a second fix. Codegen keyed a `method !foo` in the
# class method table under the BARE name, so:
#   · a class declaring `method !foo` AND `method foo` emitted one C++ body
#     twice — the generated file failed to compile, with a redefinition error
#     against code the author never wrote;
#   · `$obj."foo"()` reached the PRIVATE method, which interpreted (and under
#     Rakudo) is X::Method::NotFound.
# Private methods are now keyed `!foo`, the same key the interpreter registers
# and the same one a `self!foo` call site emits.
#
# Every case below asserts INTERPRETED == COMPILED, and that the compile stayed
# "(native)" — a silent return to bundling would pass the output checks alone.
# Contract: exit 0 + last line PASS.

my $v = run($*EXECUTABLE, '--version', :out, :err);
my $banner = $v.out.slurp(:close); $v.err.slurp(:close);
unless $banner.contains('rakupp') {
    # only rakupp has --exe; under Rakudo there is nothing this file can test
    note 'indirect-method-call-native: not rakupp, nothing to compile';
    say 'PASS';
    exit 0;
}

my $work = $*TMPDIR.add("indirect-method-native-$*PID");
mkdir $work;
my @made;
my $fails = 0;

sub check(Str $desc, $got, $want) {
    if $got eq $want { say "ok - $desc" }
    else {
        $fails++;
        say "not ok - $desc";
        note "GOT [{$got}] WANT [{$want}]";
    }
}

# Run CODE interpreted and again as a compiled binary; check both against WANT
# (so the two agree with each other, and — case 4 excepted, see there — with
# Rakudo's answer), and that the compile went down the native path rather than
# bundling.
sub agree(Str $name, Str $code, Str $want) {
    my $src = $work.add($name ~ '.raku');
    $src.spurt($code);
    my $bin = $work.add($name);
    @made.push($src.Str, $bin.Str);

    my $i = run($*EXECUTABLE, $src.Str, :out, :err);
    my $interp = $i.out.slurp(:close).lines.join('|');
    $i.err.slurp(:close);

    my $c = run($*EXECUTABLE, '--exe', '-o', $bin.Str, $src.Str, :out, :err);
    my $cout = $c.out.slurp(:close) ~ $c.err.slurp(:close);
    if $c.exitcode != 0 {
        $fails++;
        note "compile of $name failed:\n$cout";
        return;
    }
    unless $cout.contains('(native)') {
        $fails++;
        note "$name was expected to compile natively, but did not:\n$cout";
    }
    my $r = run($bin.Str, :out, :err);
    my $compiled = $r.out.slurp(:close).lines.join('|');
    $r.err.slurp(:close);

    check("$name: interpreted",     $interp,   $want);
    check("$name: compiled agrees", $compiled, $interp);
}

# 1. The report: a private method reached through a computed name.
agree('private-computed-name', q:to/END/, 'OK');
    class Foo {
        method !foo () { say "OK" }
        method bar() { my $me = "foo"; self!"$me"() }
    }
    Foo.new.bar();
    END

# 2. The public form, with positional, named and slipped arguments.
agree('public-computed-name', q:to/END/, 'hello world|HELLO WORLD|hello raku|7');
    class P { method greet($who, :$loud) { my $g = "hello $who"; $loud ?? $g.uc !! $g } }
    my $p = P.new;
    my $m = "greet";
    say $p."$m"("world");
    say $p."$m"("world", :loud);
    my @args = "raku", ;
    say $p."$m"(|@args);
    my $chars = "chars";
    say $p."$m"("x")."$chars"();
    END

# 2a. The invocant is evaluated before the name expression, and the name before
#     the arguments — C++ leaves the order of sibling arguments unspecified, so
#     codegen sequences the three through locals.
agree('evaluation-order', q:to/END/, 'bc|inv,nm,arg1,arg2');
    my @log;
    sub inv() { @log.push("inv"); "abc" }
    sub nm()  { @log.push("nm");  "substr" }
    sub arg($x) { @log.push("arg$x"); $x }
    say inv()."{nm()}"(arg(1), arg(2));
    say @log.join(",");
    END

# 3. `.$callable` / `.&sub`: a Code is CALLED with the invocant first — and a
#    type object is the coercion. The hyper form maps it over the elements.
agree('callable-and-coercion', q:to/END/, '6|11|18|2,4,6|2,4,6|(Rat)|(Int)|AB,cd');
    sub twice($x) { $x * 2 }
    sub addup($x, $y, $z = 0) { $x + $y + $z }
    my $f = &twice;
    say 3.$f;
    say 5.&addup(6);
    say 5.&addup(6, 7);
    my @a = 1, 2, 3;
    my @t = @a>>.&twice;  say @t.join(",");
    my @u = @a>>.$f;      say @u.join(",");
    my $ct = Rat;
    say (7.$ct).WHAT;
    my $x = "3";
    $x .= Int;
    say $x.WHAT;
    my @m = <ab cd>;   # `.=` with an indirect name, through a subscript lvalue
    @m[0] .= &uc;
    say @m.join(",");
    END

# 4. The metamodel form takes a computed name too. (rakupp-only: Rakudo
#    rejects an interpolated `.^` name — what is pinned here is that the two
#    rakupp faces answer alike, not parity with Rakudo.)
agree('meta-computed-name', q:to/END/, 'Str|Int');
    my $n = "name";
    say "abc".^"$n"();
    say 42.^"name"();
    END

# 5. A class declaring BOTH `method !foo` and `method foo`: two distinct
#    bodies, and the private one is not reachable from outside.
agree('private-and-public-same-name', q:to/END/, 'priv|pub|no public method|X::Method::NotFound');
    class Foo {
        method !foo() { "priv" }
        method foo()  { "pub" }
        method bar()  { self!foo() }
    }
    my $f = Foo.new;
    say $f.bar;
    say $f.foo;
    class Only { method !secret() { "hidden" } }
    my $n = "secret";
    my $r = try Only.new."$n"();
    say $r // "no public method";
    say $!.^name if $!;
    END

unlink $_ for @made;
try rmdir $work;
say $fails == 0 ?? 'PASS' !! 'FAIL';
exit($fails ?? 1 !! 0);
