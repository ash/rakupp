# Regression: the path '-' is the process's standard stream in every spelling,
# not only `'-'.IO.slurp`. `open('-')` and `'-'.IO.open` are $*IN, and with
# :w/:x/:a they are $*OUT; :rw, :update, :ra and :rx name a mode no standard
# stream has, which dies; :create, :append and :truncate alone change nothing.
# `'-'.IO.lines`, `.words`, `.comb` and `slurp('-')` read standard input.
# `open('-')` failed "No such file or directory", `open('-', :w)` made a file
# named "-", and `'-'.IO.lines` looked for "<cwd>/-". 6.d deprecates
# `open('-')` and `'-'.IO` (not `slurp('-')`), and the report names them.
# Contract: exit 0 + last line PASS.
my $dir = $*TMPDIR.add("dash-std-stream-$*PID");
$dir.mkdir;
my @fail;

sub child(Str $code, Str $in = "a\nb\n") {
    my $p = run $*EXECUTABLE, '-e', $code, :in, :out, :err, :cwd($dir.Str);
    # (a child that died is reported by its output below, not by the close)
    try { $p.in.print($in) if $in; $p.in.close; Nil }
    my $out = $p.out.slurp(:close);
    my $err = $p.err.slurp(:close);
    $out, $err
}
sub check(Str $code, Str $want, Str $in = "a\nb\n") {
    my ($out, $err) = child($code, $in);
    @fail.push("$code\n  got [{$out.lines.join('|')}] want [{$want.lines.join('|')}]  err [{$err.lines.head(2).join('|')}]")
        unless $out eq $want;
    $err
}

# reading: $*IN itself
check q[my $h = open('-'); say $h === $*IN; say $h.get; say $h.get], "True\na\nb\n";
check q[say open(:r, '-').lines.elems], "2\n";
check q[say open('-', :!chomp).get.raku], "\"a\"\n";
check q[say open('-'.IO).get], "a\n";
check q[my $h = '-'.IO.open; say $h === $*IN; say $h.get], "True\na\n";
for <create append truncate exclusive> -> $adv {
    check "say open('-', :$adv) === \$*IN", "True\n";
}
check q[say open('-', :r, :update) === $*IN], "True\n";
check q[say open('-', :mode<ro>) === $*IN], "True\n";

# writing: $*OUT itself, and no file named "-"
check q[my $h = open('-', :w); $h.say('to-out'); say $h === $*OUT], "to-out\nTrue\n", '';
check q[say open('-', :a) === $*OUT; say open('-', :x) === $*OUT], "True\nTrue\n", '';
check q[say open('-', :update, :w) === $*OUT; say open('-', :mode<wo>) === $*OUT], "True\nTrue\n", '';
check q[my $h = '-'.IO.open(:w); $h.say('io-out'); say $h === $*OUT], "io-out\nTrue\n", '';
@fail.push('a file named "-" was created') if $dir.add('-').e;

# a mode no standard stream has
check q[for <rw update ra rx> { try open('-', |($_ => True)); say $!.message }],
    "Cannot open standard stream in mode 'rw'\n" x 4, '';
check q[try open('-', :r, :w); say $!.^name, ': ', $!.message], "X::AdHoc: Cannot open standard stream in mode 'rw'\n", '';
check q[try open('-', :mode<wa>); say $!.message], "Cannot open standard stream in mode 'wa'\n", '';
check q[try '-'.IO.open(:rw); say $!.message], "Cannot open standard stream in mode 'rw'\n", '';

# the IO::Path readers
check q[say '-'.IO.lines.elems], "2\n";
check q[say '-'.IO.lines], "(a b)\n";
check q[say '-'.IO.lines(1)], "(a)\n";
check q[for '-'.IO.lines { say "<$_>" }], "<a>\n<b>\n";
check q[say '-'.IO.words], "(a b c)\n", "a b\nc\n";
check q[say '-'.IO.comb(/\w/)], "(a b c d)\n", "ab cd\n";
check q[say '-'.IO.slurp.raku], "\"a\\nb\\n\"\n";
check q[say slurp('-').raku], "\"a\\nb\\n\"\n";

# the deprecation report (6.d), and none for slurp('-') or under 6.c
my $err = check q[say open('-').get], "a\n";
@fail.push("open('-') is not reported as deprecated: [$err]") unless $err.contains('open("-") seen at');
$err = check q[say '-'.IO.lines.elems], "2\n";
@fail.push("'-'.IO.lines is not reported as deprecated: [$err]") unless $err.contains('"-".IO seen at');
$err = check q[say slurp('-').chars], "4\n";
@fail.push("slurp('-') is reported as deprecated: [$err]") if $err.contains('deprecated');
$err = check q[use v6.c; say open('-').get], "a\n";
@fail.push("open('-') under 6.c is reported as deprecated: [$err]") if $err.contains('deprecated');

$dir.add('-').unlink;
$dir.rmdir;
.say for @fail;
say @fail ?? 'FAIL' !! 'PASS';
