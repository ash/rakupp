# Regression: zef-bar campaign batch 2 — four general fixes found running
# dist test suites (File::Temp 0/3 -> 3/3; Digest::MD5 un-parse-blocked).
#
# 1. open() :rw / :exclusive / :update — all silently ignored before (mode
#    stayed "r", so `:rw, :exclusive` on a fresh path threw DoesNotExist).
#    File::Temp claims names with exactly that combination.
# 2. Positional list-declaration BIND (`my ($a, $b) := ...`) threw "Target is
#    not assignable" — only `=` and the NAMED bind form worked. File::Temp's
#    t/03 binds `my (&tempfile, &tempdir) := '...'.EVAL`.
# 3. A used module's END phaser ran AT LOAD TIME; it now defers to process
#    end and runs BEFORE the mainline's own ENDs (a module loaded later
#    cleans up earlier) — File::Temp's auto-unlink is such an END, and its
#    t/03 checks the cleanup from the test file's own END.
# 4. The lexer read `x +< n` (a spaced LETTER shift amount) as prefix-plus on
#    a word list and swallowed the rest of the line; Digest::MD5 shifts by a
#    sigilless param exactly so. A spaced letter now means shift when no `>`
#    closes a word list on the line; `+<a b>` and `+< foo bar >` stay lists.
# Contract: exit 0 + last line PASS.
my @fail;

# 1. open modes
my $p1 = $*TMPDIR.add("rakupp-om-{$*PID}-a").Str;
my $fh = open $p1, :rw, :exclusive;
@fail.push('exclusive-create') unless $p1.IO.e;
$fh.print('data');
$fh.close;
@fail.push("exclusive-content: {$p1.IO.slurp}") unless $p1.IO.slurp eq 'data';
@fail.push('exclusive-refuses') unless (try { open $p1, :rw, :exclusive; False } // True);
my $keep = $*TMPDIR.add("rakupp-om-{$*PID}-b").Str;
$keep.IO.spurt('precious');
my $fh2 = open $keep, :rw;   # untouched rw handle must NOT wipe the file
$fh2.close;
@fail.push("rw-preserves: {$keep.IO.slurp}") unless $keep.IO.slurp eq 'precious';
@fail.push('update-needs-file') unless (try { open $*TMPDIR.add("rakupp-om-{$*PID}-c").Str, :update; False } // True);
unlink($p1, $keep);

# 2. positional list bind, plain / mixed / code sigils
my ($a, $b) := (1, 2);
@fail.push('plain bind') unless $a + $b == 3;
my (&f, &g) := (sub { 20 }, sub { 22 });
@fail.push('code bind') unless f() + g() == 42;
my (:$x, :$y) := (x => 40, y => 2).hash;   # the named form keeps working
@fail.push('named bind') unless $x + $y == 42;

# 3. module END defers past the mainline's use of the module, LIFO at exit —
#    proven through a nested run so this process's own phasers stay clean
my $dir = $*TMPDIR.add("rakupp-om-{$*PID}-mod");
$dir.add('lib').mkdir;
$dir.add('lib/EndOrder.rakumod').spurt(q:to/M/);
    unit module EndOrder;
    END { print "module-end " }
    M
my $prog = 'END { print "test-end " }; use EndOrder; print "main "';
my $r = run($*EXECUTABLE.absolute, "-I{$dir.add('lib')}", '-e', $prog, :out, :err);
my $order = $r.out.slurp(:close);
$r.err.slurp(:close);
@fail.push("END order: '$order'") unless $order eq 'main module-end test-end ';
# leave nothing behind: a per-PID module directory that survives the run also
# strands a precomp entry keyed on a path that will never exist again
$dir.add('lib/EndOrder.rakumod').unlink;
$dir.add('lib').rmdir;
$dir.rmdir;

# 4. shift-vs-wordlist lexing
sub sh(\v, \n) { v +< n }
@fail.push('letter shift') unless sh(2, 1) == 4;
@fail.push('md5 shape') unless -> uint32 \v, \n { (v +< n) +| (v +> (32 - n)) }(2, 1) == 4;
my @w = +<a b>;
@fail.push("tight qw: {@w.elems}") unless @w.elems == 1;

if @fail { note "FAILED:\n" ~ @fail.join("\n"); say 'FAIL' }
else     { say 'PASS' }
