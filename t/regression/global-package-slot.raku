# Regression: `%GLOBAL::NAME` is a slot in the ROOT package's symbol table —
# empty on read, created on write — not a lexical that must already be declared.
# rakupp stripped the qualifier and looked the bare name up lexically, so a slot
# nobody had filled threw "Variable '%NAME' is not declared". Red keeps its whole
# connection registry in one (`%GLOBAL::RED-DEFAULT-DRIVERS`), declared nowhere,
# and read it before writing it.
#
# The strip itself is RIGHT and has to stay: a file-scope `our $x` lands in that
# same root package, so `$GLOBAL::x` must find it — roast S02-names/our.t asserts
# exactly that, and it is the row a first attempt at this broke.
#
# Every expectation was checked against Rakudo.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

# --- the reported shape: read before write ---------------------------------
ck(%GLOBAL::RAKUPP-PROBE-DRIVERS.defined, False, 'an unfilled slot reads as undefined');
ck(?(%GLOBAL::RAKUPP-PROBE-DRIVERS || 'fallback'), True, '…and is falsy, so `||` takes the other branch');
%GLOBAL::RAKUPP-PROBE-DRIVERS<default> = 'sqlite';
ck(%GLOBAL::RAKUPP-PROBE-DRIVERS<default>, 'sqlite', 'a write creates the slot');
ck(%GLOBAL::RAKUPP-PROBE-DRIVERS<default>:exists, True, '…and :exists sees it');
ck(%GLOBAL::RAKUPP-PROBE-DRIVERS<missing>:exists, False, '…and does not see what is not there');

# --- the other sigils ------------------------------------------------------
ck($GLOBAL::RAKUPP-PROBE-SCALAR.defined, False, 'a scalar slot reads undefined');
$GLOBAL::RAKUPP-PROBE-SCALAR = 42;
ck($GLOBAL::RAKUPP-PROBE-SCALAR, 42, '…and takes a value');
@GLOBAL::RAKUPP-PROBE-ARRAY.push(7);
ck(@GLOBAL::RAKUPP-PROBE-ARRAY.List, (7,), 'an array slot autovivifies');

# --- and the root package is where a file-scope `our` lives ---------------
our $rakupp-probe-our = 1;
$GLOBAL::rakupp-probe-our++;
ck($rakupp-probe-our, 2, '`$GLOBAL::x` IS the file-scope `our $x`');
ck($GLOBAL::rakupp-probe-our, 2, '…read back through either spelling');

say $fails ?? "FAIL ($fails)" !! "PASS";
exit $fails ?? 1 !! 0;
