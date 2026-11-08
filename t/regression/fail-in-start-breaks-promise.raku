# A `fail` written in a `start` block throws, as Rakudo's does with no routine
# to return a Failure from, so the promise is Broken with its exception: it
# returned the Failure from the block's own frame, and the promise was Kept
# with it. A `Failure.new` the block answers is still a kept value, a routine
# the block calls still returns its Failure, and a `start` inside a routine
# breaks the same.
# Every line answers the same under Rakudo 2026.09.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}

my $p = start { fail "f" };
await Promise.anyof($p, Promise.in(5));
ck ($p.status, $p.cause.message), (Broken, 'f'), 'fail in a start block';
my $q = start { Failure.new("q") };
await Promise.anyof($q, Promise.in(5));
ck $q.status, Kept, 'a Failure value is kept';
sub g { fail "g" }
my $r = start { my $x = g(); $x.handled = True; "kept" };
ck (await $r), 'kept', "a routine's fail returns its Failure";
sub h { start { fail "h" } }
my $s = h();
await Promise.anyof($s, Promise.in(5));
ck $s.status, Broken, 'a start inside a routine';

say 'PASS';
