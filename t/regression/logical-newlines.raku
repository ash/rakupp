# `.lines` breaks a Str on the whole LOGICAL NEWLINE set — LF, CR, CRLF, FF, VT,
# NEL, LS, PS — not just "\n" and "\r\n". Splitting on "\n" alone (with a
# trailing "\r" trimmed) left a lone CR, an old-Mac line ending, inside the line.
# `.chomp` had the same short set.
#
# A FILE's `.lines` is deliberately NOT this set (its nl-in is ["\n", "\r\n"]),
# and that stays as it was.
#
# The same scanner drives `.lines`/`.words` on a live Supply, where the message
# boundaries are not the piece boundaries: a tail that could still GROW into a
# terminator (a lone "\r" before an unseen "\n", a truncated NEL/LS/PS) is held
# for the next message. Found via S17-supply/lines.t once `Supply.lines` existed
# to run it (issue #34).
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $desc) {
    @fail.push("$desc: got {$got.raku}, want {$want.raku}") unless $got eqv $want;
}

# --- Str.lines over every terminator ----------------------------------------
my %sep = LF => "\n", CR => "\r", CRLF => "\r\n", FF => "\x0C",
          VT => "\x0B", NEL => "\x[85]", LS => "\x[2028]", PS => "\x[2029]";
for %sep.sort(*.key) -> (:key($name), :value($s)) {
    check "a{$s}b".lines.List, ('a', 'b'), ".lines breaks on $name";
    check "a{$s}".chomp,       'a',        ".chomp removes a trailing $name";
    check "a{$s}b".lines(:!chomp).List, ("a$s", 'b'), ".lines(:!chomp) keeps the $name";
}
check "a\r\nb".lines.List, ('a', 'b'), 'CRLF is ONE terminator, not two';
check "\r\n\n".lines.List, ('', ''),   'two terminators, two empty lines';
check "a".lines.List,      ('a',),     'an unterminated last line';
check "a\n".lines.List,    ('a',),     'a terminated last line yields no empty tail';
check "a\rb\rc".lines(:count), 3,      ':count counts CR-separated lines';
check "a\rb\rc\rd".lines(2).List, ('a', 'b'), 'a positional limit still applies';
check "a\rb".chomp, "a\rb", '.chomp removes only a TRAILING newline';

# a FILE keeps the narrow set — its nl-in is ["\n", "\r\n"]
my $f = $*TMPDIR.add("rakupp-nl-{$*PID}.txt");
$f.spurt("a\rb\nc\n");
LEAVE { try $f.unlink }
check $f.lines.List,       ("a\rb", 'c'), "a FILE's .lines does not break on a lone CR";
check $f.slurp.lines.List, ('a', 'b', 'c'), '…but slurping it into a Str does';

# --- the same set on a live Supply, across message boundaries ---------------
sub drain(&build, *@messages) {
    my $s = Supplier.new;
    my @got;
    build($s.Supply).tap(-> $v { @got.push($v) });
    $s.emit($_) for @messages;
    $s.done;
    @got.List;
}
check drain({ $^s.lines }, "a\rb\r"),        ('a', 'b'), 'Supply.lines breaks on CR';
check drain({ $^s.lines }, "a\r", "\nb\n"),  ('a', 'b'),
      'a CRLF SPLIT ACROSS MESSAGES is one terminator, not two';
check drain({ $^s.lines }, "a\nb\r", "\nc\rd\n", "\ne", "eee"),
      ('a', 'b', 'c', 'd', '', 'eeee'),
      'the chunked case from S17-supply/lines.t';
check drain({ $^s.lines(:!chomp) }, "a\r", "\nb"), ("a\r\n", 'b'),
      ':!chomp keeps a terminator that arrived in two pieces';

# --- .live: only map/grep carry a live source through ------------------------
my $sup = Supplier.new;
check $sup.Supply.live,                True,  'a Supplier.Supply is live';
check $sup.Supply.map(*.self).live,    True,  '.map keeps it live';
check $sup.Supply.grep(*.so).live,     True,  '.grep keeps it live';
check $sup.Supply.lines.live,          False, '.lines does not';
check $sup.Supply.head(2).live,        False, '.head does not';
check Supply.from-list(1, 2).live,     False, 'a list-backed Supply was never live';

if @fail {
    note "FAILED:\n" ~ @fail.map({ "  - $_" }).join("\n");
    exit 1;
}
say "PASS";
