# Regression: a module that fails to compile during `use` was located in the
# LOADING script.
#
# The header named the module and its line correctly, but the excerpt under it
# ("NNN | …") and the trailing "at FILE:NNN" read line NNN of the program being
# run — a line that has nothing to do with the error, or no excerpt at all when
# the script is shorter than NNN. Seen on nige123/cli.321.do, where a parse
# error on line 221 of lib/Do321/Trust.rakumod quoted line 221 of
# t/06-run.rakutest. ParseError now carries the module's file.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check(Bool $ok, $what) { @fail.push($what) unless $ok }

my $dir = $*TMPDIR.add("modparseloc-{$*PID}");
$dir.mkdir;
$dir.add('lib').mkdir;
my $lib = $dir.add('lib');

# The parse error is on line 4 of each module.
$lib.add('BadLoc.rakumod').spurt: q:to/END/;
    unit module BadLoc;
    my %h = a => 1;
    my $x = 1;
    say ($x && %h<a>:exists);  # MODULE-LINE-FOUR
    END
$lib.add('BadInner.rakumod').spurt: q:to/END/;
    unit module BadInner;
    my %h;
    my $x;
    say ($x && %h<b>:exists);  # INNER-LINE-FOUR
    END
$lib.add('BadOuter.rakumod').spurt: q:to/END/;
    unit module BadOuter;
    use BadInner;
    say "OUTER-LINE-THREE";
    say "OUTER-LINE-FOUR";
    END

# Line 4 of the script says something else, so a wrong excerpt shows.
my $long = $dir.add('long.raku');
$long.spurt: q:to/END/;
    use BadLoc;
    say "SCRIPT-LINE-TWO";
    say "SCRIPT-LINE-THREE";
    say "SCRIPT-LINE-FOUR";
    END
# Shorter than the module's error line: the excerpt used to vanish.
my $short = $dir.add('short.raku');
$short.spurt: "use BadLoc;\n";
my $nested = $dir.add('nested.raku');
$nested.spurt: q:to/END/;
    use BadOuter;
    say "NESTED-LINE-TWO";
    say "NESTED-LINE-THREE";
    say "NESTED-LINE-FOUR";
    END

sub err($script) {
    my $p = run($*EXECUTABLE, '-I', ~$lib, ~$script, :out, :err);
    $p.out.slurp(:close);
    $p.err.slurp(:close)
}

for $long, $short -> $script {
    my $e = err($script);
    my $n = $script.basename;
    check $e.contains('Error while compiling module BadLoc'), "$n: header names the module";
    check so($e ~~ / '4 | ' .* 'MODULE-LINE-FOUR' /),          "$n: excerpt is the module's line 4";
    check !$e.contains('SCRIPT-LINE-FOUR'),                     "$n: excerpt is not the script's line 4";
    check so($e ~~ / ^^ \s* 'at ' \S* 'BadLoc.rakumod:4' $$ /), "$n: 'at' names the module file";
    check !$e.contains("$n:4"),                                 "$n: 'at' does not name the script";
}

{
    my $e = err($nested);
    check so($e ~~ / '4 | ' .* 'INNER-LINE-FOUR' /),             'nested: excerpt is the inner module\'s line';
    check !$e.contains('NESTED-LINE-FOUR') && !$e.contains('OUTER-LINE-FOUR'),
                                                                  'nested: no excerpt from the outer files';
    check so($e ~~ / ^^ \s* 'at ' \S* 'BadInner.rakumod:4' $$ /), 'nested: \'at\' names the inner module';
}

.unlink for $long, $short, $nested, |$lib.dir;
$lib.rmdir;
$dir.rmdir;

if @fail { note "FAILED: @fail.join('; ')"; say 'FAIL' } else { say 'PASS' }
