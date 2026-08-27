# Regression: an UNDEFINED value through a `--> subset` boundary.
#
# The post-3.7.0 "a SUBSET return type constrains even an UNDEFINED value"
# check rejected EVERY type object, but Rakudo's rule is finer: the nominal
# base chain is checked first, then the where clauses run — and core UInt is
#     subset UInt of Int where { not .defined or $_ >= 0 }
# so an Int-derived type object passes `--> UInt` (and any refinement-free
# subset over it) SILENTLY, while a hash-miss Any still fails nominally and a
# refining where still rejects. URI 0.3.8 died on exactly this:
# `method port(--> Port)` with `our subset Port of UInt` returns the undefined
# Port attribute when neither the URI nor its scheme carries a port, and the
# whole suite fell over test 48. Fixed by typeMatchesResolved: for TYPE
# OBJECTS a subset name resolves through its base chain, and UInt conforms as
# Int does.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub case(Str $code, Str $want, Str $what, Bool :$dies = False) {
    my $p = run($*EXECUTABLE, '-e', $code, :out, :err);
    my $out = $p.out.slurp(:close).chomp;
    my $err = $p.err.slurp(:close);
    if $dies {
        @fail.push("$what: expected death, got exit 0, out={$out.raku}") if $p.exitcode == 0;
        @fail.push("$what: died without naming the type ({$err.lines.head // ''})")
            unless $err.contains($want);
    }
    else {
        @fail.push("$what: exit {$p.exitcode}, err={$err.lines.head // ''}") if $p.exitcode != 0;
        @fail.push("$what: got {$out.raku} want {$want.raku}") if $out ne $want;
    }
}

# the shapes that must PASS the boundary
case 'sub f(--> UInt) { my Int %h; %h<x> }; print f().^name',
     'Int', 'hash-miss Int through --> UInt';
case 'subset P of UInt; sub f(--> P) { my Int %h; %h<x> }; print f().^name',
     'Int', 'hash-miss Int through --> subset-of-UInt';
case 'subset Port of UInt; class A { has Port $.port }; sub f(--> Port) { A.new.port }; print f().defined ?? "def" !! "undef"',
     'undef', 'the URI shape: an undefined subset-typed attribute through its own subset';
case 'subset Port of UInt; sub f(--> Port) { 8080 }; print f()',
     '8080', 'a defined value still passes';

# the shapes that must still DIE
case 'sub f(--> UInt) { my %h; %h<x> }; f()',
     'expected UInt but got Any', 'a hash-miss Any still fails UInt nominally', :dies;
case 'subset P of Int where * > 5; sub f(--> P) { Int }; f()',
     'expected P but got Int', 'a refining where still rejects the type object', :dies;
case 'subset P of Int where * > 5; sub f(--> P) { 3 }; f()',
     'expected P but got Int', 'a defined value failing the where still dies', :dies;

if @fail {
    .say for @fail;
    say "FAIL";
    exit 1;
}
say "PASS";
