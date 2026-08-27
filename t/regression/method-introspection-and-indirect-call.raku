# What it takes to look a method up and then CALL it — the shape
# `$object.$method(|$capture)` that Path::Finder uses for every matcher it
# resolves by name through `^lookup`.
#
#   1. A method's `.signature.count`/`.arity` count the INVOCANT. Rakudo reports
#      `method file(Bool $v = True)` as count 2 / arity 1, and code that asks how
#      many arguments a method takes subtracts one for it. With the invocant
#      uncounted every matcher looked unusable.
#   2. A multi group's signature is its PROTO's. `^lookup` hands back the
#      dispatcher, whose own signature is empty, so a proto-declared method
#      reported `()` and count 0.
#   3. An indirect call splats a capture: `$obj.$m(|$cap)` passed the capture as
#      one Slip argument, because that path evaluated its arguments raw.
#   4. A method GROUP invoked as a callable takes its invocant as the first
#      positional. It went down the SUB dispatcher instead, scored the invocant
#      as an ordinary argument and matched no candidate at all.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $desc) {
    @fail.push("$desc: got {$got.raku}, want {$want.raku}") unless $got eqv $want;
}

class C {
    method file(Bool $v = True) { "file:$v" }
    method many(*@a)            { "many:{+@a}" }
    method two($a, $b)          { "two:$a$b" }
    proto method rp(Mu $p)      { * }
    multi method rp(Str $p)     { "rp-str:$p" }
    multi method rp(Int $p)     { "rp-int:$p" }
}

# --- 1 + 2: what introspection reports ---------------------------------------
check C.^lookup('file').signature.count, 2, 'count includes the invocant';
check C.^lookup('file').signature.arity, 1, 'arity includes the invocant';
check C.^lookup('two').signature.count,  3, 'count with two positionals';
check C.^lookup('two').signature.arity,  3, 'arity with two positionals';
check C.^lookup('many').signature.count, Inf, 'a slurpy still counts Inf';
check C.^lookup('many').signature.arity, 1,   '…and its arity is just the invocant';
check C.^lookup('rp').signature.count,   2, "a multi group reports its PROTO's count";
check C.^lookup('rp').signature.arity,   2, '…and its arity';
check C.^lookup('file').signature.params[0].invocant, True, 'params[0] is the invocant';
check C.^lookup('file').signature.params[0].name,     '',   '…and it is anonymous';
check C.^lookup('rp').WHAT.^name, 'Method', 'a method group is a Method, not a Sub';

# --- 3 + 4: calling what was looked up ---------------------------------------
my $c = C.new;
my $cap0 = \();
my $cap1 = \(False);
my $cap2 = \('a', 'b');
my $mfile = C.^lookup('file');
my $mtwo  = C.^lookup('two');
my $mrp   = C.^lookup('rp');
check $c.$mfile(|$cap1), 'file:False', 'indirect call splats a 1-arg capture';
check $c.$mtwo(|$cap2),  'two:ab',     '…and a 2-arg one';
check $c.$mfile(|$cap0), 'file:True',  '…and an empty one takes the default';
check $c.$mrp(|\('s')),  'rp-str:s',   'a multi GROUP dispatches on the splatted arg';
check $c.$mrp(|\(7)),    'rp-int:7',   '…and picks the other candidate';

# the same call written the ways that already worked
my $m = C.^lookup('file');
check $c.$m(True),    'file:True',  'an indirect call with a plain argument';
check $c.file(|$cap1),'file:False', 'a direct call still splats';
check $c.rp('s'),     'rp-str:s',   'a direct multi call is unchanged';

if @fail {
    note "FAILED:\n" ~ @fail.map({ "  - $_" }).join("\n");
    exit 1;
}
say "PASS";
