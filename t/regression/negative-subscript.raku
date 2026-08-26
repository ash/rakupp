# Issue #36: `@a[-1]` was accepted. A negative subscript does not index from the
# end in Raku — `*-1` does — and Rakudo refuses the literal spelling at COMPILE
# time (X::Obsolete). rakupp only ever made it a run-time X::OutOfRange
# (be0c4c2), so `rakupp -c -e 'my @foo; @foo[-1] = 1'` said "Syntax OK".
#
# Guarded here: the spellings that must be refused, the near-misses that must
# still parse (the rule is textual and narrow — matching Rakudo's narrowness is
# what keeps it from refusing a program Rakudo runs), and the POSITIONAL METHODS
# that were still wrapping Python-style while the subscript path threw.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) { @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want }

my $bin = $*EXECUTABLE.absolute;
sub parses($src) { run($bin, '-c', '-e', $src, :out, :err).so }

# --- refused at compile time ------------------------------------------------
for '@a[-1]', '@a[-2]', '@a[ -1 ]', '@a[-0]', '@a.[-1]', '@a[-1;2]', '@a[2;-1]',
    '@a[-1]:exists', '@a[0][-1]', '@a»[-1]', '"@a[-1]"',
    '@a[0..-1]', '@a[ 0 .. -42 ]', '@a[0^..-1]', '@a[0...-1]', '@a[-1..-1]'
    -> $sub {
    check(parses("my @a; my \$x = 1; \{ $sub \}"), False, "refuses $sub");
}

# --- still parses: the rule is `-` glued to plain decimal digits, nothing after
for '@a[- 1]', '@a[-1_0]', '@a[-0x10]', '@a[-1.0]', '@a[-1e0]', '@a[(-1)]',
    '@a[-1, 2]', '@a[-1..1]', '@a[-1-1]', '@a[--1]', '@a[+-1]', '@a[*-1]', '@a[$x]',
    '@a[0..^-1]', '@a[0^..^-1]', '@a[0..- 1]', '@a[0..(-1)]', '@a[0..-$x]',
    '@a[0..-1, 2]', '@a[0..-1+0]', '%h{-1}', '%h{0..-1}', '"@a[- 1]"'
    -> $sub {
    check(parses("my @a; my \$x = 1; my %h; \{ $sub \}"), True, "still parses $sub");
}

# --- the diagnostic itself (roast S02-types/array.t asserts this prose) ------
my $err = run($bin, '-c', '-e', 'my @a; @a[-2]', :out, :err).err.slurp(:close);
check($err.contains('Unsupported use of a negative -2 subscript to index from the end'), True,
      'the message names the index');
check($err.contains('a function such as *-2'), True, 'and points at *-2');

# --- and it is X::Obsolete, not a bare parse failure -------------------------
{
    use MONKEY-SEE-NO-EVAL;
    try EVAL 'my @a; @a[-1]';
    check($!.^name, 'X::Obsolete', 'EVAL raises X::Obsolete');
    check($!.old, 'a negative -1 subscript to index from the end', '.old');
    check($!.replacement, 'a function such as *-1', '.replacement');
}

# --- the positional methods agree with the subscript path -------------------
my @a = 10, 20, 30;
check((try @a.AT-POS(-1)) // $!.^name, 'X::OutOfRange', 'AT-POS(-1) is out of range');
check((try @a.ASSIGN-POS(-1, 99)) // $!.^name, 'X::OutOfRange', 'ASSIGN-POS(-1) too');
check((try @a.DELETE-POS(-1)) // $!.^name, 'X::OutOfRange', 'DELETE-POS(-1) too');
check(@a, [10, 20, 30], 'and none of them touched the array');
check(@a.EXISTS-POS(-1), False, 'EXISTS-POS(-1) is False, not an error');
check(@a.AT-POS(99), Any, 'a positive index past the end is still Any');
check(@a.EXISTS-POS(99), False, 'and False');
check((try (1, 2, 3).AT-POS(-1)) // $!.^name, 'X::OutOfRange', 'a list says the same');

# --- an indirect negative index is a run-time error, not a compile-time one --
check(parses('my @a; my $i = -1; @a[$i]'), True, 'a variable index compiles');
my $i = -1;
check((try @a[$i]) // $!.^name, 'X::OutOfRange', 'and throws when it runs');

if @fail { note "FAIL:\n" ~ @fail.join("\n"); exit 1 }
say 'PASS';
