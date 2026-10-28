# Regression: a LEAVE that dies while a METHOD unwinds leaves the caller's
# scope in place for its CATCH (issue #205).
#
# `method open($p) { my $r = R.new(…); LEAVE $r.close; 1 }` with a `.new`
# that dies: the LEAVE then dies too ($r is Any). The method's handler ran
# the LEAVEs and only then put the caller's scope back, so the second error
# skipped that line, and the caller's CATCH ran in the method's scope:
# `$why = .message` there was "Variable '$why' is not declared". A sub was
# right — its frame restores the scope however it is left — and a method
# now does the same.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}

class R { method new(|) { die "cannot open file" }; method close { } }
class H {
    method open($p) { my $r = R.new(path => $p); LEAVE $r.close; 1 }
    method guarded($p) { my $r = R.new(path => $p); LEAVE { .close with $r }; 1 }
    method leave-dies { LEAVE die "from LEAVE"; 1 }
}

sub caller-of(&call) {
    my ($v, $why);
    {
        $v = call();
        CATCH { default { $why = .message } }
    }
    $why
}

ck caller-of({ H.open('x') }).starts-with("No such method 'close'"), True,
   "the LEAVE's error reaches the caller's CATCH, which sees its own lexicals";
ck caller-of({ H.guarded('x') }), 'cannot open file', 'a LEAVE that lives: the first error';
ck caller-of({ H.leave-dies }), 'from LEAVE', 'a LEAVE that dies on a normal exit';

# …and the caller carries on in its own scope afterwards
my $after = 'unset';
{
    H.open('x');
    CATCH { default { $after = 'caught' } }
}
ck $after, 'caught', 'a block-level CATCH in the mainline';
my $still = 42;
ck $still, 42, 'the mainline scope is intact';

say 'PASS';
