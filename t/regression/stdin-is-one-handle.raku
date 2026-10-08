# Regression: standard input is ONE handle, however `$*IN` is reached. Every
# mention of `$*IN` made a fresh handle onto the stream, so a copy stopped
# being `===` to `$*IN` once a read went through it, and `$*IN.close` closed
# only the mention it was called on: `$*IN.opened` stayed True and reads went
# on, and `$*IN.lines(:close)` / `.words(:close)` closed nothing. A closed
# standard input refuses `get`, `lines` and `words`, as methods and as subs,
# with X::IO::Closed; `open('-')` opens it again.
# Contract: exit 0 + last line PASS.
my @fail;

sub check(Str $code, Str $want, Str $in = "a\nb\n") {
    my $p = run $*EXECUTABLE, '-e', $code, :in, :out, :err;
    # (a child that died is reported by its output below, not by the close)
    try { $p.in.print($in) if $in; $p.in.close; Nil }
    my $out = $p.out.slurp(:close);
    my $err = $p.err.slurp(:close);
    @fail.push("$code\n  got [{$out.lines.join('|')}] want [{$want.lines.join('|')}]  err [{$err.lines.head(2).join('|')}]")
        unless $out eq $want;
}

# one object
check q[my $h = $*IN; $h.get; say $h === $*IN], "True\n";
check q[my $h = $*IN; say $h.lines.elems; say $h === $*IN], "2\nTrue\n";
check q[say $*ARGFILES === $*IN; say $*IN === $*OUT], "True\nFalse\n";
check q[my %h = std => 'in'; my %g = std => 'in'; say %h === %g], "False\n";

# closed for every mention, and for a copy made before
check q[$*IN.close; say $*IN.opened; say $*IN.eof], "False\nTrue\n";
check q[my $h = $*IN; $*IN.close; say $h.opened], "False\n";
check q[my $h = $*IN; $h.close; say $*IN.opened], "False\n";
check q[start { $*IN.close }.result; say $*IN.opened], "False\n";
check q[$*IN.close; for <get lines words> -> $m { try $*IN."$m"(); say $!.^name, ' ', $!.trying }],
    "X::IO::Closed get\nX::IO::Closed lines\nX::IO::Closed words\n";
check q[$*IN.close; try get(); say $!.^name; try lines(); say $!.^name; try words(); say $!.^name],
    "X::IO::Closed\nX::IO::Closed\nX::IO::Closed\n";
check q[$*IN.close; my $h = open('-'); say $h.opened; say $*IN.opened], "True\nTrue\n";

# :close closes it when the lines (or words) run out, not before
check q[say $*IN.lines(:close).elems; say $*IN.opened], "2\nFalse\n";
check q[my $l = $*IN.lines(1, :close); say $*IN.opened; say $l; say $*IN.opened], "True\n(a)\nFalse\n";
check q[my $l = $*IN.lines(:close); say $l[0]; say $*IN.opened; say $l.elems; say $*IN.opened], "a\nTrue\n2\nFalse\n";
check q[say $*IN.words(:close); say $*IN.opened], "(a b)\nFalse\n";
check q[say $*IN.lines(:!close).elems; say $*IN.opened], "2\nTrue\n";

# a limit takes that many lines and leaves the rest
check q[say $*IN.lines(1); say $*IN.get], "(a)\nb\n";
check q[say $*IN.lines(0); say $*IN.get], "()\na\n";

.say for @fail;
say @fail ?? 'FAIL' !! 'PASS';
