# Regression: a module's variables are not the importing program's
# (review ledger O30).
#
# Loading a module published every variable of its top-level scope into
# GLOBAL under its BARE name — its `my`s and its unexported `our`s alike. So
# after `use UM`, where UM is `unit module UM; our $v = 1; …`, the program's
# own `our $v` was UM's `$v` (1, then 2 after UM's `bump()`, where Rakudo
# answers Any both times), and `::('$secret')` found a module's private `my`.
# Only the variables a module exports reach its importer by their bare
# names; an `our` goes by its package's name (`$UM::v`).
# Answers are Rakudo 2026.09's.
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) { @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want }

my $dir = $*TMPDIR.add("module-vars-$*PID");
$dir.mkdir;
$dir.add('ModVarsUM.rakumod').spurt: q:to/END/;
    unit module ModVarsUM;
    our $v = 1;
    sub bump is export { our $v; $v++; $v }
    our sub get { our $v }
    END
$dir.add('ModVarsUM3.rakumod').spurt: q:to/END/;
    unit module ModVarsUM3;
    my $secret = 42;
    our $pub = 7;
    our $exv is export = 5;
    our @arr = 1, 2;
    sub peek is export { $secret }
    END
sub child(Str $code) {
    my $p = run $*EXECUTABLE, "-I$dir", '-e', $code, :out, :err;
    my $out = $p.out.slurp(:close);
    $p.err.slurp(:close);
    $out.lines.List
}

check child(Q[use ModVarsUM; say (our $v).raku; bump(); say (our $v).raku; say $ModVarsUM::v; say ModVarsUM::get()]),
      ('Any', 'Any', '2', '2'), "the program's our is not the module's";
check child(Q[use ModVarsUM3; say ::('$secret') ~~ Failure; say ::('$pub') ~~ Failure; say ::('@arr') ~~ Failure]),
      ('True', 'True', 'True'), "a module's my and unexported our are not GLOBAL's bare names";
check child(Q[use ModVarsUM3; say $ModVarsUM3::pub; say @ModVarsUM3::arr; say $exv; say peek()]),
      ('7', '[1 2]', '5', '42'), '…while its package names, its exports and its own routines see them';

sub rmtree(IO::Path $d) { for $d.dir { .d ?? rmtree($_) !! .unlink }; $d.rmdir }
rmtree($dir);   # (with the .precomp a compiler may have left)

if @fail { note "FAILED:\n  " ~ @fail.join("\n  "); say 'FAIL' } else { say 'PASS' }
