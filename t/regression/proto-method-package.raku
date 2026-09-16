# Regression: a method GROUP belongs to the class its candidates do.
#
# `proto method pick(|) is protected {*}` applies a trait to the proto, and a
# trait handler reads `$method.package` to find the class it should modify. An
# ordinary method carried its declaring type; the multi-dispatcher built for a
# proto or for the first `multi method` carried no package at all, so `.package`
# fell back to GLOBAL — and Method::Protected then asked GLOBAL for
# `^add_attribute`, which is not a method GLOBAL has.
#
# The declaring class is the answer for the group as much as for a candidate:
# Rakudo hands back the same package for both.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) {
    @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eq $want
}

my @seen;
multi sub trait_mod:<is>(Method:D $m, :$tagme!) {
    @seen.push: $m.name ~ '=' ~ $m.package.^name;
}

class Klass {
    method plain() is tagme { 1 }
    proto method pro(|) is tagme {*}
    multi method pro(Int) { 2 }
    multi method pro(Str) { 3 }
}

check @seen.grep(*.starts-with('plain')).head, 'plain=Klass', 'an ordinary method knows its class';
check @seen.grep(*.starts-with('pro')).head,   'pro=Klass',   'and so does a proto';

# A multi with no proto builds the group too, and it needs the same answer.
class Other {
    multi method solo(Int) is tagme { 1 }
    multi method solo(Str) { 2 }
}
check @seen.grep(*.starts-with('solo')).head, 'solo=Other', 'a protoless multi group too';

# The dispatch itself must still work — the package is metadata, not plumbing.
check Klass.pro(1),  2, 'the proto still dispatches on Int';
check Klass.pro('x'), 3, 'and on Str';
check Other.solo(1),  1, 'the protoless group dispatches too';

# …and `^lookup` hands back the group, which is a Method, not a Sub.
check Klass.^lookup('pro').^name, 'Method', 'the group is still a Method';

if @fail {
    .say for @fail;
    say "FAIL";
    exit 1;
}
say "PASS";
