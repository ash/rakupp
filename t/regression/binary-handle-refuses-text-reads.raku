# Regression: a binary handle has no characters to read.
#
# `open(:bin)`, and a handle whose text was switched off with `.encoding(Nil)`
# or `.encoding("bin")`, answered `.get`, `.lines`, `.words`, `.getc`,
# `.readchars`, `.comb` and `.split` with text, as if it were not binary.
# Rakudo dies with X::IO::BinaryMode ("Cannot do 'get' on a handle in binary
# mode"); `.slurp` and `.read` answer bytes, and `.encoding("utf8")` makes it
# a text handle again. `$*IN` keeps the setting, so `lines()` and `words()`
# refuse too. `.readchars(-1)` answered "" where Rakudo dies. Found while
# fixing the 2026-10-21 review's O23 (ledger O35).
#
# Runs clean under Rakudo too.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}
sub refusal(&code) {
    my $r = try code();
    $! ?? ($!.^name, ($!.?trying // ''), $!.message) !! ('answered', '', $r.raku)
}

my $f = $*TMPDIR.add("rakupp-bin-text-reads-$*PID.txt");
LEAVE { $f.unlink if $f.e }
$f.spurt: "abc\ndef\n";

for <get getc readchars lines words> -> $m {
    my $h = $f.open(:bin);
    ck refusal({ $h."$m"() }),
       ('X::IO::BinaryMode', $m, "Cannot do '$m' on a handle in binary mode"), "open(:bin).$m";
    $h.close;
}
{
    my $h = $f.open(:bin);
    ck refusal({ $h.comb(/\w/) }).head, 'X::IO::BinaryMode', 'open(:bin).comb';
    ck refusal({ $h.split("\n") }).head, 'X::IO::BinaryMode', 'open(:bin).split';
    $h.close;
}
# …what a binary handle does read
{
    my $h = $f.open(:bin);
    ck $h.read(3).list.List, (97, 98, 99), 'open(:bin).read';
    ck $h.slurp.list.List, (10, 100, 101, 102, 10), '…and .slurp answers the rest as bytes';
    $h.close;
}

# text switched off, and on again
{
    my $h = $f.open;
    $h.encoding(Nil);
    ck refusal({ $h.get }).head, 'X::IO::BinaryMode', '.encoding(Nil) makes it binary';
    $h.close;
}
{
    my $h = $f.open;
    $h.encoding('bin');
    ck refusal({ $h.lines }).head, 'X::IO::BinaryMode', '.encoding("bin") too';
    $h.close;
}
{
    my $h = $f.open(:bin);
    $h.encoding('utf8');
    ck $h.get, 'abc', '.encoding("utf8") makes a binary handle text again';
    $h.close;
}

# a negative count is an error, a zero one is ""
{
    my $h = $f.open;
    ck so(try { $h.readchars(-1); True }), False, '.readchars(-1) dies';
    ck $h.readchars(0), '', '.readchars(0) is ""';
    $h.close;
}

# ---- $*IN keeps the setting, for the subs that read it as well ----
my $exe = $*EXECUTABLE.absolute;
sub child(Str $code) {
    my $p = run $exe, '-e', $code, :in, :out;
    $p.in.print("ab\ncd\n");
    $p.in.close;
    $p.out.slurp(:close).trim
}
ck child(Q[$*IN.encoding(Nil); print (try $*IN.get) // $!.^name]),          'X::IO::BinaryMode', '$*IN.get';
ck child(Q[$*IN.encoding(Nil); print (try lines().List) // $!.^name]),      'X::IO::BinaryMode', 'lines()';
ck child(Q[$*IN.encoding(Nil); print (try words().List) // $!.^name]),      'X::IO::BinaryMode', 'words()';
ck child(Q[$*IN.encoding(Nil); print $*IN.slurp.list.elems]),               '6', '$*IN.slurp is bytes';
ck child(Q[$*IN.encoding(Nil); $*IN.encoding("utf8"); print $*IN.get]),     'ab', '…and text again';

say $fails ?? "FAIL ($fails)" !! 'PASS';
