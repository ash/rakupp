# Regression: two gaps JSON::Unmarshal's own test suite found (via the
# `rakupp install` test gate, which refused the dist):
#
# 1. CONTROL { when CX::Warn } did not exist: `warn` printed straight to
#    stderr, never raising an interceptable control exception. Now warn runs
#    the innermost dynamically-enclosing CONTROL handler IN PLACE (no
#    unwinding, so .resume is resumable by construction; the handler is
#    popped while it runs, so its own warns escape outward). A handler that
#    .resume's suppresses the default print; one that does not leaves the
#    default behaviour standing. CONTROL blocks also no longer masquerade as
#    CATCH — a real die passes straight through them.
#
# 2. DateTime.^attributes was empty (every builtin answered the honest empty
#    list). JSON::Unmarshal rebuilds a DateTime from a JSON object shaped
#    like Rakudo's internals ($!year … $!daycount, &!formatter), so Date and
#    DateTime now answer the Rakudo-shaped list; all other builtins keep the
#    empty answer.
#
# Every check verified against Rakudo (this file runs on both engines).
#
# Contract: exit 0 + last line PASS.
my @fail;

sub check($got, $want, $desc) {
    @fail.push("$desc: got {$got.raku}, want {$want.raku}") unless $got eqv $want;
}

# ---- CONTROL / CX::Warn -----------------------------------------------------
{
    my $caught = '';
    {
        CONTROL {
            when CX::Warn {
                $caught = .message;
                .resume
            }
        }
        warn "intercepted";
        $caught ~= "|continued";
    }
    check $caught, 'intercepted|continued', 'CONTROL sees CX::Warn, .resume continues after the warn';
}

{
    my $caught = '';
    sub deep-warner {
        warn "from a sub"
    }
    {
        CONTROL {
            when CX::Warn {
                $caught = .message;
                .resume
            }
        }
        deep-warner();
    }
    check $caught, 'from a sub', 'a warn inside a CALLED sub reaches the dynamically-enclosing CONTROL';
}

{
    my $died = '';
    try {
        CONTROL {
            when CX::Warn {
                .resume
            }
        }
        die "real error";
    }
    check ~$!, 'real error', 'a CONTROL block does not swallow a real die';
}

# ---- DateTime.^attributes ---------------------------------------------------
my @names = DateTime.^attributes.map(*.name);
check @names.elems, 9, 'DateTime.^attributes answers all nine';
check ?(@names.first(* eq '$!year')), True, '…including $!year';
check ?(@names.first(* eq '&!formatter')), True, '…and &!formatter';
check ?(DateTime.^attributes.first({ .has_accessor })), True, 'the flags satisfy introspection-driven rebuilds';
check Date.^attributes.map(*.name).elems, 5, 'Date answers its five';
# (Int would differ: Rakudo exposes its boxed $!value, rakupp's builtins
# honestly answer empty — Any is attribute-free on both engines)
check Any.^attributes.elems, 0, 'other builtins keep the empty answer';

if @fail {
    .say for @fail;
    say "FAIL";
    exit 1;
}
say "PASS";
