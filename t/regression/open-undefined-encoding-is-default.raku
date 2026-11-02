# Regression: `open($f, :enc(Str))` — an UNDEFINED encoding is the default one.
#
# open's `:enc` loop skipped only Any, so a `Str` type object (and Nil) was
# read as the encoding '' and refused: "Unknown string encoding ''". Text::CSV
# passes its `Str :$encoding` straight on as `:enc($encoding)`, so every
# `csv(in => $file)` that named no encoding died in its own t/67_emptrow.t,
# and `rakupp install Text::CSV` would not install it. Rakudo opens all of
# these as utf8; a DEFINED unknown name is still refused.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($what, $got, $want) {
    @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want
}

my $f = $*TMPDIR.add("rakupp-undef-enc-{$*PID}.txt");
$f.spurt("a,b\n\n1,2\n");
LEAVE $f.unlink;

my Str $e;
sub first-line(|c) { my $h = open $f, |c; LEAVE .close with $h; $h.get }

check 'open :enc(Str)',  first-line(:enc($e)),     'a,b';
check 'open :enc(Nil)',  first-line(:enc(Nil)),    'a,b';
check 'open :encoding',  first-line(:encoding($e)), 'a,b';
check 'open :!chomp :enc(Str)', first-line(:!chomp, :enc($e)), "a,b\n";
check 'IO.open :enc(Str)', do { my $h = $f.IO.open(:enc($e)); LEAVE $h.close; $h.get }, 'a,b';
check ':bin with :enc(Str) is no conflict',
    do { my $h = open $f, :bin, :enc($e); LEAVE $h.close; $h.read(3) }, Buf[uint8].new(97, 44, 98);

# as Text::CSV has it: a typed named parameter handed on
sub csvish(Str :$encoding is copy) { first-line(:r, :!chomp, :enc($encoding)) }
check 'a Str :$encoding left out', csvish(), "a,b\n";

# a defined name still has to be one
check 'open :enc("") dies', (try { first-line(:enc("")); 'lived' }) // $!.^name, 'X::Encoding::Unknown';
check 'open :enc("bogus") dies', (try { first-line(:enc("bogus")); 'lived' }) // $!.^name, 'X::Encoding::Unknown';

if @fail {
    .say for @fail;
    say "FAIL";
    exit 1;
}
say "PASS";
