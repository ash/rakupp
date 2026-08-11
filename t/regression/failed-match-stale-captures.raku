# Regression: after a FAILED match, bare $0/$1 still answered the captures of
# an EARLIER successful match.
#
# A successful match (and a substitution's replacement eval) defines $0..$N as
# real variables in the scope current at match time, because $N is sugar for
# $/[N] and $/ alone can't carry them into every lookup context. A failed match
# reset $/ to Nil but left those bindings standing, so in
#
#     $baz ~~ s{.(a)(.)} = "$1$0p";   # file scope: $1 is now ｢z｣
#     { "abc" ~~ m/^(nope)/; say ?$1 }  # was True — the stale ｢z｣, not Nil
#
# the block saw the leaked binding instead of falling through to the reset $/.
# Rakudo's contract is that $N always tracks $/: after a failed match every $N
# is Nil. Found by S05-modifier/Perl_0.t test 21 while building m:P5 support;
# the bug itself is general and this repro uses plain Raku regexes only.
#
# Fixed in Interpreter::regexMatch — on a failed match, every $k that still
# resolves is shadowed with Nil so the alias contract holds.
#
# Contract: exit 0 + last line PASS.
my @fail;

sub check($got, $want, $desc) {
    @fail.push("$desc: got {$got.raku}, want {$want.raku}") unless $got eqv $want;
}

# a substitution leaves $/ (and so $0/$1) on the last match, Rakudo-style
my $baz = "baz";
$baz ~~ s{.(a)(.)} = "$1$0p";
check $baz, "zap", 'the substitution itself';
check (~$0), "a", '$0 after s/// is the first capture';
check (~$1), "z", '$1 after s/// is the second capture';

# a failed match resets the whole family, not just $/
{
    "abc" ~~ m/^(nope)/;
    check $/, Nil, '$/ is Nil after a failed match';
    check (?$0), False, '$0 is falsy after a failed match';
    check (?$1), False, '$1 is falsy after a failed match (was the stale ｢z｣)';
}

# a successful match still repopulates after a failure
{
    "abc" ~~ m/^(nope)/;
    "xy" ~~ m/(x)(y)/;
    check (~$0), "x", '$0 tracks the newest successful match';
    check (~$1), "y", '$1 tracks the newest successful match';
}

if @fail {
    note "FAILED:\n" ~ @fail.map({ "  - $_" }).join("\n");
    exit 1;
}
say "PASS";
