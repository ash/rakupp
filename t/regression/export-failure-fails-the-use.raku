# Regression: a `die` inside a module's `sub EXPORT` was swallowed, so a failing
# `use` was only a warning and the program ran on at exit 0.
#
# Not a bug a change introduced — the catch was there from the start, written for
# ONE distribution. The `if` dist's EXPORT necessarily fails here (both of its
# implementations patch Rakudo compiler internals; rakupp supplies the `:if`
# adverb natively), and that dist is already exempted BY NAME in the same block.
# The blanket swallow around it was collateral: it made every other module's
# export-time validation advisory, so `use M <typo>` imported nothing, said so
# only on stderr, and ran the program anyway.
#
# The two halves that make this narrow, and both are measured against Rakudo:
#
#   `use`/`need`  — Rakudo aborts compilation and exits 1. So does rakupp now.
#   `require`     — Rakudo does not run a module's `sub EXPORT` AT ALL, in any
#                   of its spellings. rakupp does run it, so propagating there
#                   would fail a load that Rakudo completes; `require` keeps the
#                   warn-and-continue it always had.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) {
    @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eq $want
}

my $dir = $*TMPDIR.add("exportfail-{$*PID}");
$dir.mkdir;
$dir.add('DiesInExport.rakumod').spurt:
    'sub EXPORT(*@) { die "DiesInExport: deliberate refusal" }' ~ "\n";
$dir.add('GoodExport.rakumod').spurt:
    'sub EXPORT(*@) { Map.new((q{&hello} => sub { "hi" })) }' ~ "\n";

sub run-it(Str $code) {
    my $p = run($*EXECUTABLE, '-I', ~$dir, '-e', $code, :out, :err);
    my $out = $p.out.slurp(:close);
    my $err = $p.err.slurp(:close);
    ($p.exitcode, $out, $err)
}

# --- `use`: the failure is the use's, and it is fatal -----------------------
my ($rc, $out, $err) = run-it('use DiesInExport; say "RAN"');
check $rc != 0,                       True,  'a failing EXPORT makes `use` exit non-zero';
check $out.contains('RAN'),           False, 'and the program does not run';
check $err.contains('deliberate refusal'), True, "and the module's own message is what is reported";

# --- `require`: Rakudo runs no EXPORT there, so this must not be stricter ---
($rc, $out, $err) = run-it('require DiesInExport; say "RAN"');
check $rc,                  0,     '`require` of the same module still succeeds';
check $out.contains('RAN'), True,  'and the program runs — Rakudo runs no EXPORT for require';

# --- a working EXPORT is untouched -----------------------------------------
($rc, $out, $err) = run-it('use GoodExport; say hello()');
check $rc,                 0,      'a successful EXPORT still loads';
check $out.contains('hi'), True,   'and its symbols are in scope';

# --- the one exemption -----------------------------------------------------
# `use if` must keep loading. Skipped when the dist is not installed rather than
# failing, so the case stays useful on a machine without it.
my $has-if = run($*EXECUTABLE, '-e', 'use if;', :out, :err).so;
if $has-if {
    ($rc, $out, $err) = run-it('use if; say "IF-OK"');
    check $rc,                    0,    '`use if` is still exempt';
    check $out.contains('IF-OK'), True, 'and the program runs';
}

# Recursive, and tolerant: Rakudo leaves a `.precomp` tree in here, so a plain
# rmdir fails on the very engine whose behaviour this case is written against.
sub nuke(IO::Path $p) {
    if $p.d { nuke($_) for $p.dir; try $p.rmdir }
    else { try $p.unlink }
}
nuke($dir);

if @fail { die "FAIL:\n" ~ @fail.join("\n") }
say 'PASS';
