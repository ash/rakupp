# A regex literal IS a Regex object. What matches `$_` is BOOLIFYING one — `if
# /b/` and `if $rx` both match, and both set `$/` — plus a bare regex STATEMENT
# in sink context, which is what such a statement is written for.
#
# rakupp had the default the other way round: evaluating a literal matched `$_`,
# and each position that needed the object (an assignment, an argument, a list, a
# hash value) opted out syntactically. Every position with no opt-out — a block's
# last statement, an explicit `return`, a ternary branch, the right side of `&&` —
# answered a Match, or Nil when `$_` was unset. Path::Finder builds its glob
# matchers by reducing regexes into one (`.reduce({ /$^a$^b/ })`), which is a
# block's last statement, so every glob it built matched nothing (issue #34).
#
# Two things fall out of the flip and are fixed here too: the value-level `~~`
# had never learned about a Regex operand (matching was always decided
# syntactically), and a retired Perl 5 metachar has to be refused when the regex
# is CONSTRUCTED, since nothing may ever match with it.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $desc) {
    @fail.push("$desc: got {$got.raku}, want {$want.raku}") unless $got eqv $want;
}

$_ = 'abc';

# --- a literal is a Regex wherever its VALUE is taken ------------------------
check (my $a = /b/).^name,            'Regex', 'assigned';
check (do { /b/ }).^name,             'Regex', "a block's last statement";
check (sub { /b/ })().^name,          'Regex', "a sub's return value";
check (sub { return /b/ })().^name,   'Regex', 'an explicit return';
check ({ /b/ })().^name,              'Regex', 'a called block';
check ((/b/, 1)[0]).^name,            'Regex', 'an element of a list';
check ({ k => /b/ }<k>).^name,        'Regex', 'a hash value';
check (1 && /b/).^name,               'Regex', 'the right side of &&';
check (True ?? /b/ !! /c/).^name,     'Regex', 'a ternary branch';
check (sub ($p, $q) { /$p$q/ })(/a/, /b/).^name, 'Regex', 'composed from two regexes';

# --- boolifying one matches $_, and sets $/ ---------------------------------
check (if /b/ { 'M' } else { 'n' }), 'M',  'a literal in a condition matches $_';
check (if /b/ { ~$/ } else { 'n' }), 'b',  '…and sets $/';
my $rx = /b/;
check (if $rx { 'M' } else { 'n' }), 'M',  'a STORED regex matches $_ too';
check ?$rx,  True,  '…under ?';
check ?/z/,  False, '…and answers False when it does not match';
check (so /b/), True, 'so /b/';

# --- a bare regex STATEMENT in sink context matches and sets $/ -------------
$/ = Nil;
/c/;
check ~$/, 'c', 'a sink-position regex statement matches and sets $/';
$/ = Nil;
my $val = do { /a/ };
check $/.defined, False, '…where a VALUE-position one does not';
check $val.^name, 'Regex', '…and answers the Regex';

# --- m// still matches, rx// is still always the object ---------------------
check (my $m = m/b/).^name, 'Match', 'm// matches even in value position';
check (my $r = rx/b/).^name, 'Regex', 'rx// is the object, as it always was';

# --- the value-level ~~ knows a Regex --------------------------------------
my $digits = /^\d+$/;
check ('12' ~~ $digits).Bool, True, 'a Regex value on the right of ~~';
sub w($x where /^\d+$/) { "ok:$x" }
check w('12'), 'ok:12', 'a `where` clause that is a bare regex';
$_ = 'abc';

# --- a retired Perl 5 metachar is refused when the regex is CONSTRUCTED -----
# (in a child process: it is a compile-time refusal, so it cannot sit inline)
for '\\A', '\\Z', '\\z', '\\G' -> $seq {
    my $f = $*TMPDIR.add("rakupp-obsolete-{$*PID}.raku");
    $f.spurt("my \$r = /{$seq}abc/; print 'NO-THROW';");
    LEAVE { try $f.unlink }
    my $p = run($*EXECUTABLE, $f.Str, :out, :err);
    my $out = $p.out.slurp(:close); $p.err.slurp(:close);
    check $out, '', "a regex holding $seq is refused when constructed";
}

if @fail {
    note "FAILED:\n" ~ @fail.map({ "  - $_" }).join("\n");
    exit 1;
}
say "PASS";
