# Regression: a zen slice DECONTAINERISES. `$x<>`, `$x[]`, `$x{}` (and the
# dotted `.<>` / `.[]` / `.{}`) were parsed as no-ops, which is right for `@a`
# and `%h` — never itemised — and wrong for a Scalar: `for $aoa<>` then walked
# the ONE item the Scalar is instead of the elements it holds. Reported on a
# JSON::Fast array-of-arrays, which comes back as `$[[…], […]]` and so came out
# nested one level too deep (issue #55). `.<>` and `.{}` did not even parse.
#
# Every expectation below was checked against Rakudo.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

# --- the reported shape: an array of arrays out of a Scalar ----------------
my $aoa = [["A"], ["B"]];
my @out;
for $aoa<> -> $g { push @out, $g<> }
ck(@out.raku, '[["A"], ["B"]]', 'for $aoa<> walks the elements, not the item');
ck((do { my $n = 0; $n++ for $aoa<>; $n }), 2, 'and iterates as many times as there are elements');

# --- all three spellings, plain and dotted --------------------------------
my $arr = [1, 2];
my $hsh = { x => 1 };
ck($arr<>.raku,   '[1, 2]',   '$x<> drops the item container');
ck($arr[].raku,   '[1, 2]',   '$x[] does too');
ck($arr{}.raku,   '[1, 2]',   'and $x{} — a zen slice returns self, whatever the brackets');
ck($arr.<>.raku,  '[1, 2]',   '$x.<> parses and decontainerises');
ck($arr.[].raku,  '[1, 2]',   '$x.[] as well');
ck($arr.{}.raku,  '[1, 2]',   '$x.{} as well');
ck($hsh<>.raku,   '{:x(1)}',  'a Scalar-held Hash decontainerises');
ck($hsh.<>.raku,  '{:x(1)}',  'through the dotted form too');
my $lst = (1, 2, 3);
ck($lst<>.raku,   '(1, 2, 3)', 'a List stays a List, minus the itemisation');

# --- and it stays the no-op it always was where nothing is containerised ---
my @a = 1, 2, 3;
my %h = a => 1;
ck(@a<>.raku, '[1, 2, 3]', '@a<> is @a');
ck(%h<>.raku, '{:a(1)}',   '%h<> is %h');
ck(("x"<>).raku, '"x"', 'a Str passes through');
ck((5<>).raku,   '5',   'so does an Int');
my Any $undef;
ck($undef<>.raku, 'Any', 'and a type object');

# --- the container is still reachable: subscripts and assignment ----------
my $w = [1, 2];
$w<>[0] = 9;
ck($w.raku, '$[9, 2]', '$x<>[0] = … writes through the container');
my $wh = { a => 1 };
$wh<><a> = 7;
ck($wh.raku, '${:a(7)}', 'and so does $x<><key> = …');
my %wa = a => 1;
%wa<> = (b => 2);
ck(%wa.raku, '{:b(2)}', '%h<> = … still assigns the whole hash');
my @wl = 1, 2;
@wl<> = 5, 6;
ck(@wl.raku, '[5, 6]', '@a<> = … likewise');

# --- the topic aliases the ELEMENTS, exactly as `for @($x)` does -----------
my $rw = [1, 2];
for $rw<> -> $x is rw { $x++ }
ck($rw.raku, '$[2, 3]', 'for $x<> -> $e is rw writes into the array');
my $rw2 = [1, 2];
$_++ for $rw2<>;
ck($rw2.raku, '$[2, 3]', 'and the statement-modifier form does too');

# --- an ADVERBED zen slice still hangs its adverb on an Index -------------
my %ad = a => 1, b => 2;
ck(%ad<>:k.sort.List.raku, '("a", "b")', '%h<>:k keeps the adverb');

# --- the Perl 5 null filehandle is still refused --------------------------
ck((try { EVAL 'my @x = <>;' } // 'refused'), 'refused', 'a bare <> in term position is still an error');
ck(($! ~~ X::Obsolete).so, True, 'and it is still X::Obsolete');

say $fails ?? "FAIL ($fails)" !! "PASS";
exit $fails ?? 1 !! 0;
