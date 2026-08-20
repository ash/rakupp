# Regression: three from the Weekly Challenge sweep, all in one author's files.
#
# 1. A `where` constraint sees the EARLIER parameters — `multi f($l, $n where *
#    > $l)` compares the two arguments. A plain sub already did (the constraint
#    is checked as each parameter binds), but MULTI DISPATCH evaluated the
#    constraint with only the parameter itself in scope, so any cross-parameter
#    constraint threw and the candidate silently lost.
#
# 2. `m:ov` / `m:overlap` — like `:g`, but the next search starts one character
#    past the last match's START, so matches may overlap. It was not
#    implemented at all: the adverb was ignored and a single Match came back.
#
# 3. A code assertion's `$/` is the CURSOR: `<?{ $/.chars == 2 }>` and
#    `<?{ $0 eq $2 }>` ask about the match SO FAR. The assertion hook evaluated
#    its code with whatever `$/` the surrounding scope held — usually an
#    unrelated or empty match — so those assertions were simply false.
#
# 4. …and a Range in NUMERIC comparison is its ELEMENT COUNT (`Range.Numeric`
#    is `.elems`), so `4 > 1..3` asks 4 > 3. rakupp compared the Range as a
#    structure and `2 > (1..4)` was True.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) {
    @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eq $want
}

# --- 1. a where that reads an earlier parameter ---
multi pick-one($l, $n where * > $l) { 'constrained' }
multi pick-one($l, $n)              { 'plain' }
check pick-one(3, 4),      'constrained', 'a cross-parameter where wins the dispatch';
check pick-one(4, 3),      'plain',       '…and loses it when it does not hold';
multi blocky($l, $n where { $n > $l }) { 'constrained' }
multi blocky($l, $n)                   { 'plain' }
check blocky(3, 4), 'constrained', '…in the block spelling too';
# the plain-sub path is unchanged
sub only-one($l, $n where * > $l) { 'ok' }
check only-one(3, 4), 'ok', 'a plain sub still binds it';

# --- 2. :ov ---
check ("abab" ~~ m:ov/ab/).elems, 2, ':ov finds the non-overlapping ones too';
check ("aaaa" ~~ m:ov/aa/).elems, 3, '…and the overlapping ones';
check ("aaaa" ~~ m:overlap/aa/).elems, 3, '…under the long name';
check ("aaaa" ~~ m:g/aa/).elems, 2, 'while :g still steps past each match';
check ("abc" ~~ m:ov/\d/).elems, 0, 'and no match is an empty list';

# --- 3. the cursor inside a code assertion ---
check ("24" ~~ / \d ** {2} <?{ $/.chars == 2 }> /).Str, '24', '$/ is the match so far';
check ("24" ~~ / (\d) (\d) <?{ $0 eq "2" }> /).Str,     '24', '…and $0 is its capture';
check ("ab" ~~ / (.) <?{ $0 eq "b" }> . /).defined,   False, '…so a false assertion still fails';
check (240 ~~ m:ov/ \d ** {2} <?{ 240 %% $/ }> /).elems, 2, 'the two together, as the solution wrote it';
check ("aaaaa" ~~ m:ov/ (.) (.*) (.) <?{ $0 eq $2 }> /).elems, 4, '…and with three captures';

# --- 4. a Range compares by its element count ---
check ((1..3) == 3),    True,  'a Range numifies to its elems';
check (2 > (1..4)),     False, '…so 2 is not more than four elements';
check (4 > (1..3)),     True,  '…and 4 is more than three';
check ((1..3) < (1..5)), True, '…two Ranges compare the same way';
check ((1..Inf) > 5),   True,  'an endless Range is Inf, not materialised';
check (("a".."c") == 3), True, '…and a Str Range counts its elements';
check ((1.5..3.7) == 3), True, 'a fractional Range steps by one from its start';
check ((1..3) eqv (1..3)), True, 'eqv is still structural';
check (2 ~~ (1..4)),    True,  'and smartmatch is still membership';

if @fail { note "FAILED: " ~ @fail.join('; '); say 'FAIL' } else { say 'PASS' }
