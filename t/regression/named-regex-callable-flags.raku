# Regression: `my rule R {…}` matched with the WRONG FLAGS when it was reached
# through its `&R` Callable rather than as the `<R>` subrule. The `<R>` path has
# always mapped the declarator's kind to its flags — rule is sigspace+ratchet,
# token is ratchet, regex is neither — but the Callable ran the bare pattern, so
# a rule's `<.ws>` was never inserted. `my rule u { ab cd }` therefore matched
# "abcd" and REFUSED "ab cd" through `$s ~~ &u`: the exact inverse of what the
# same rule does as `<u>`.
#
# Found in Crane (issue #69), which tests an exception payload against
# `my rule can-not-remove { Can not remove [values|elements] from a (\w+) }`
# inside a CATCH. The rule never matched, so `.payload !~~ &can-not-remove` was
# always true, the `or die(...)` never fired, and the `when` block completed
# normally — which HANDLES the exception. Every immutability refusal was
# swallowed there and the operation reported success.
#
# Runs under both engines: Rakudo passes every check natively. (Rakudo warns
# about significant space in a non-sigspace regex, so the `regex`/`token` rows
# below use quoted atoms.)
#
# Contract: exit 0 + last line PASS.
my @fail;

sub check($got, $want, $desc) {
    @fail.push("$desc: got {$got.raku}, want {$want.raku}") unless $got eqv $want;
}

# --- a rule is sigspace through &name, exactly as it is through <name> -------
my rule two-words { ab cd }
check ('ab cd' ~~ &two-words).Bool, True,  'a rule matches across whitespace through &name';
check ('abcd'  ~~ &two-words).Bool, False, '…and does NOT match without it';
check ('ab cd' ~~ / <two-words> /).Bool, True, 'the <name> spelling agrees';
check ('abcd'  ~~ / <two-words> /).Bool, False, '…in both directions';

my rule quoted-atoms { 'ab' 'cd' }
check ('ab cd' ~~ &quoted-atoms).Bool, True, 'quoted atoms are spaced too';

my rule three-words { Can not remove }
check ('Can not remove' ~~ &three-words).Bool, True, 'three bareword atoms';

# the shape Crane actually relies on: a capture out of a rule matched through &name
my rule can-not-remove { Can not remove [values|elements] from a (\w+) }
my $payload = 'Can not remove values from a Pair';
check ($payload ~~ &can-not-remove).Bool, True, 'a rule with a capture matches through &name';
check ($payload !~~ &can-not-remove),     False, '…so the negated form is False';
check ~($payload ~~ &can-not-remove)[0], 'Pair', '…and the capture is the type name';

# --- a regex and a token keep their own flavours -----------------------------
my regex plain { 'ab' \s* 'cd' }
check ('ab cd' ~~ &plain).Bool, True,  'a regex is not sigspace but still matches its own spelling';
check ('abcd'  ~~ &plain).Bool, True,  '…with the whitespace optional because IT says so';

my token tok { 'ab' 'cd' }
check ('abcd'  ~~ &tok).Bool, True,  'a token concatenates its atoms';
check ('ab cd' ~~ &tok).Bool, False, '…and does not space them';

# --- and a rule used in a sub, which is where Crane meets it ----------------
sub matches($s) { $s ~~ &can-not-remove }
check matches($payload).Bool, True, 'the same rule called from inside a sub';

if @fail {
    note $_ for @fail;
    die "{+@fail} check(s) failed";
}
say 'PASS';
