# Regression: EVALFILE (issue #33) was never registered — `EVALFILE($path)`
# died with "Undefined routine 'EVALFILE'", which blocked Tomtit and, in the
# ecosystem sweep, Device::HIDAPI and Notcurses::Native.
#
# Rakudo's is `EVAL slurp($filename), :$lang, |%adverbs`: the file is read
# FIRST (a missing one dies before the language is looked at), the code is
# evaluated in the CALLER's lexical scope, and every adverb rides through to
# EVAL unchanged.
# Contract: exit 0 + last line PASS.
my @fail;
my $dir = $*TMPDIR.add("rakupp-evalfile-{$*PID}");
$dir.mkdir;
my $f = $dir.add('snippet.raku');
$f.spurt("32 + 10\n");

# the plain call, and the same file named by IO::Path
@fail.push('Str path')      unless EVALFILE($f.absolute) == 42;
@fail.push('IO::Path')      unless EVALFILE($f) == 42;

# evaluated code sees the caller's lexicals (S29-context/evalfile.t)
my $some_var = 'samovar';
$f.spurt('$some_var' ~ "\n");
@fail.push('sees lexicals') unless EVALFILE($f.absolute) eq 'samovar';

# …and declares INTO that scope, as EVAL does
$f.spurt("my \$from-file = 7;\n");
EVALFILE($f.absolute);
@fail.push('declares into the scope') unless $from-file == 7;

# it is a real routine value, not a parser special case
my &e = &EVALFILE;
$f.spurt("32 + 10\n");
@fail.push('&EVALFILE') unless e($f.absolute) == 42;

# a missing file dies the way slurp does — before :lang is even considered
my $missing = $dir.add('no-such-file.raku').absolute;
my $err = '';
try { EVALFILE($missing, :lang<Perl5>); CATCH { default { $err = .message } } }
@fail.push("missing file: $err") unless $err.contains('Failed to open file')
                                     && $err.contains($missing);

# an unknown language is EVAL's X::Eval::NoSuchLang, not a bare string
my $lang-err = '';
try { EVALFILE($f.absolute, :lang<BrainfuckPP>); CATCH { default { $lang-err = .^name } } }
@fail.push("lang: $lang-err") unless $lang-err eq 'X::Eval::NoSuchLang';

# :lang<Raku> / :lang<Perl6> are the two spellings that DO compile
@fail.push('lang Raku')  unless EVALFILE($f.absolute, :lang<Raku>)  == 42;
@fail.push('lang Perl6') unless EVALFILE($f.absolute, :lang<Perl6>) == 42;

unlink($f);
rmdir($dir);

if @fail { note "FAILED:\n" ~ @fail.join("\n"); say 'FAIL' }
else     { say 'PASS' }
