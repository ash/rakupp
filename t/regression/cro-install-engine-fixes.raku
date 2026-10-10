# Regression, 2026-10-10: the engine gaps the test suites of Cro::HTTP,
# Cro::WebSocket and Docker::File ran into on the way to `rakupp install cro`
# (and `cro --help` after it), each answered as Rakudo 2026.09 answers it:
#   - a proto alternative calling a QUALIFIED subrule (`<dt=G::rule>`) still
#     competes by its literal prefix (the cookie parser's `Expires=`)
#   - `{ name <word> => … }` is a block: a call with an angle argument, not a
#     subscript (the router's `{ delegate <foo> => $app }`)
#   - `return` inside a tap's quit handler leaves the routine around the emit
#   - an Encoding::Decoder decodes in its own encoding; its default line
#     separators take a CRLF whole, and an explicit "\n" leaves one alone
#   - decoded text and program text are NFC: U+2126 OHM SIGN is U+03A9
#   - List.new / Array.new flatten a Slip argument
#   - a `$!x` / `:$!x` parameter is type-checked as an assignment to the
#     attribute, subsets included; `:@!a` keeps the element type
#   - `.schedule-on($*SCHEDULER)` runs the tap's callbacks off the emitter
#   - a `die` in a whenever's LAST quits the supply
#   - Parameter.constraint_list, and a "double-quoted" literal parameter
#     reports its literal (the link generator's static path segments)
#   - a subset-typed parameter introspects as its nominal type plus the
#     subset as a constraint; `int8.^nativesize`, `uint8.^unsigned`
#   - a `where` binding failure carries `.parameter`
#   - a flattening slurpy keeps an Array's elements whole
#   - an invocant typed by a short class name means the one where the method
#     was written, though another class took that short name first
#   - a `/` inside `<< … >>` / `« … »` is a word character
#   - `.schedule-on` is as `.live` as its source
#   - an explicit `proto MAIN(|)` still falls back to the usage message
use Test;
plan 59;

# a qualified subrule call in a proto alternative
grammar DateG { token date { \d ** 4 '-' \d\d } }
my regex path { <[\x1F..\xFF] - [;]>+ }
grammar CookieAV {
    token TOP { <av> }
    proto token av {*}
    token av:sym<expires> { :i 'Expires=' <dt=DateG::date> }
    token av:sym<ext>     { <path> }
}
with CookieAV.parse('Expires=2020-01') {
    ok .<av><dt>.defined, 'the alternative calling DateG::date wins';
    is .<av><dt>.Str, '2020-01', '…and captures through it';
}
else { flunk 'the cookie attribute parses' for ^2 }

# `{ call <word> => value }` is a block
sub delegate(Pair $p) { $p.key ~ ':' ~ $p.value }
my $blk = { delegate <foo> => 42 };
isa-ok $blk, Block, '{ delegate <foo> => 42 } is a block';
is $blk(), 'foo:42', '…which calls delegate with a Pair';
my %hh = a => 1;
my $hsh = { %hh<a> => 2 };
isa-ok $hsh, Hash, '{ %h<a> => 2 } is still a hash';

# `return` from a quit handler, reached through Supplier.emit
sub refuses() {
    my $in = Supplier.new;
    my $t = supply { whenever $in.Supply { die "bad" } };
    $t.tap: -> $v { return "from-emit" }, quit => -> $e { return "from-quit: $e.message()" };
    $in.emit(1);
    "fell-through"
}
is refuses(), 'from-quit: bad', 'return in a quit handler leaves the routine';

# Encoding::Decoder
{
    my $d = Encoding::Registry.find('iso-8859-1').decoder;
    $d.set-line-separators(["\r\n", "\n"]);
    $d.add-bytes(Blob.new(0x61, 0xE6, 0xB5, 0xA5, 0x0D, 0x0A, 0x62));
    is $d.consume-line-chars(:chomp), "a\x[E6]\x[B5]\x[A5]", 'latin-1 bytes decode as latin-1';
    is $d.consume-all-chars, 'b', '…and the rest';
    my $p = Encoding::Registry.find('latin-1').decoder;
    $p.add-bytes(Blob.new(0x61, 0x0D));
    is $p.consume-available-chars, 'a', 'a trailing CR waits for its LF';
    is $p.bytes-available, 1, '…in the buffer';
    my $u = Encoding::Registry.find('utf-16le').decoder;
    $u.set-line-separators(["\n"]);
    $u.add-bytes("héllo\nwörld".encode('utf-16le'));
    is $u.consume-line-chars(:chomp), 'héllo', 'a utf-16 decoder splits on an encoded separator';
    is $u.consume-all-chars, 'wörld', '…and decodes the rest';
    my $w = Encoding::Registry.find('windows-1252').decoder;
    $w.add-bytes(Blob.new(0x80, 0x41));
    is $w.consume-all-chars, '€A', 'windows-1252 has its own upper half';
    my $c = Encoding::Registry.find('utf-8').decoder;
    $c.add-bytes("a\r\nb\nc\rd\n".encode);
    is $c.consume-line-chars(:chomp), 'a', 'the default separators take a CRLF whole';
    is $c.consume-line-chars(:chomp), 'b', '…and a LF';
    is $c.consume-line-chars(:chomp), "c\rd", '…but not a lone CR';
    my $e = Encoding::Registry.find('utf-8').decoder;
    $e.set-line-separators(["\n"]);
    $e.add-bytes("a\r\nb\n".encode);
    is $e.consume-line-chars(:chomp), "a\r\nb", 'an explicit "\n" does not split a CRLF';
}

# NFC: decoded text, and the program's own
{
    my $ohm = Blob.new(0xE2, 0x84, 0xA6).decode;
    is $ohm.ords, (0x3A9,), 'Blob.decode gives NFC';
    ok $ohm eq "\x[3A9]", '…so it equals GREEK CAPITAL OMEGA';
    my $dec = Encoding::Registry.find('utf-8').decoder;
    $dec.add-bytes(Blob.new(0xE2, 0x84, 0xA6, 0x0A));
    ok $dec.consume-line-chars(:chomp) eq "\x[3A9]", 'an Encoding::Decoder gives NFC';
    my %k = "\x[2126]" => 1;
    is %k{"\x[3A9]"}, 1, 'a hash key written with OHM SIGN is OMEGA';
    is EVAL("my \$\x[2126]x = 5; \$\x[3A9]x"), 5, 'a name written with OHM SIGN is the OMEGA one';
    is EVAL('my %h = ' ~ "\x[2126]" ~ ' => 1; %h<' ~ "\x[3A9]" ~ '>'), 1, '…and so is a bareword key';
}

# List.new / Array.new with a Slip
class MultiV is List { }
is List.new(slip(1, 3), 4).raku, '(1, 3, 4)', 'List.new flattens a Slip';
is Array.new(slip(1, 3), 4).raku, '[1, 3, 4]', '…and Array.new';
is MultiV.new((1, 3).Slip, 4).elems, 3, '…and a List subclass';
is List.new(Empty, 1).raku, '(1,)', 'Empty adds nothing';

# attributive parameters are type-checked
subset Positive of Int where * > 0;
class BuildPos { has Positive $.x; submethod BUILD(:$!x) {} }
throws-like { BuildPos.new(:x(-1)) }, X::TypeCheck::Assignment, 'BUILD(:$!x) checks a subset attribute';
is BuildPos.new(:x(3)).x, 3, '…and takes a value that fits';
class SetPos { has Positive $.x; method set($!x) { } }
throws-like { SetPos.new.set(-2) }, X::TypeCheck::Assignment, 'method set($!x) checks too';
class BuildArr { has Int @.a; submethod BUILD(:@!a) {} }
throws-like { BuildArr.new(:a<x y>) }, X::TypeCheck::Assignment, 'BUILD(:@!a) checks the elements';
is BuildArr.new(:a(1, 2)).a.of.^name, 'Int', '…and keeps the element type';

# schedule-on hops off the emitting thread; LAST may quit
{
    my $s = Supplier.new;
    my $main = $*THREAD.id;
    my $off = Promise.new;
    $s.Supply.schedule-on($*SCHEDULER).tap: { $off.keep($*THREAD.id != $main) };
    $s.emit(1);
    ok (await Promise.anyof($off, Promise.in(10))) && $off.result, 'schedule-on delivers off the emitter';
    my $l = Supplier.new;
    my $quit = '';
    supply { whenever $l.Supply { LAST { die "short" } } }.tap: -> $ { }, quit => { $quit = .message };
    $l.done;
    is $quit, 'short', 'a die in LAST quits the supply';
}

# Parameter.constraint_list
sub routed("greet", $name where *.chars > 0, $rest) { }
my @rp = &routed.signature.params;
is @rp[0].constraint_list.raku, '("greet",)', 'a "literal" parameter is its own constraint';
is @rp[0].type.^name, 'Str', '…and is typed by it';
is @rp.map(*.constraint_list.elems).join(','), '1,1,0', 'a where clause is one constraint, none is none';

# a subset-typed parameter; native sizes
my subset Pct of Int where 1..100;
sub pct(Pct $p, UInt $u) { }
my @pp = &pct.signature.params;
is @pp[0].type.^name, 'Int', 'a subset parameter is typed by its nominal type';
is @pp[0].constraint_list.map(*.^name).join, 'Pct', '…and has the subset as its constraint';
is @pp[1].type.^name, 'Int', 'UInt is a subset of Int';
is &pct.signature.raku, ':(Int $p where { ... }, Int $u where { ... })', '…and renders as one';
ok 200 !~~ @pp[0].constraints, 'the constraint refuses what the subset refuses';
is (int8.^nativesize, uint16.^nativesize, num32.^nativesize).join(','), '8,16,32', '.^nativesize';
is (int8.^unsigned, uint8.^unsigned, byte.^unsigned).join(','), '0,1,1', '.^unsigned';

# a `where` failure carries its parameter
sub advent(Int $day where * <= 24) { }
try advent(|\(25));
is $!.^name, 'X::TypeCheck::Binding::Parameter', 'a where failure is a binding failure';
is $!.parameter.name, '$day', '…carrying the parameter';
nok $!.parameter.named, '…which is positional';
sub opt(Int :$n where * > 1) { }
try opt(:n(0));
ok $!.parameter.named, 'a named one says so';

# slurpies keep an Array's elements whole
sub count-slurped(*@c) { @c.elems }
my @pairs-of = (1, 2), (3, 4);
is count-slurped(@pairs-of), 2, 'an Array of Lists is two arguments';
is count-slurped([(1, 2, 3),]), 1, '[(…),] is one';
is count-slurped((1, [2, 3])), 3, 'an Array inside a List flattens';
is count-slurped(Array.new((1, 2), (3, 4))), 2, 'Array.new((…), (…)) is two';

# a short invocant type is the one where the method was written
class X::Shadow::OnBuild is Exception { }
class Shadow {
    class OnBuild { multi method Str(OnBuild:D:) { 'onbuild' } }
}
is Shadow::OnBuild.new.Str, 'onbuild', 'OnBuild:D: is Shadow::OnBuild inside Shadow';

# a slash in << >> and « »
is <</foo /bar/baz>>.raku, '("/foo", "/bar/baz")', 'a /path in << >> is a word';
is «a /b/ c».raku, '("a", "/b/", "c")', '…and in « »';
my @neg = -« (1, 2);
is @neg.raku, '[-1, -2]', '-« stays a prefix hyper';

# schedule-on liveness
ok Supplier.new.Supply.schedule-on($*SCHEDULER).live, 'schedule-on of a live supply is live';

# `proto MAIN(|)` and the usage fallback
{
    my $script = $*TMPDIR.add("rakupp-proto-main-{$*PID}.raku");
    $script.spurt: q:to/CODE/;
        proto MAIN(|) {*}
        multi MAIN('web', Str $host-port = '10203') { say "web $host-port" }
        CODE
    my $p = run $*EXECUTABLE, $script, '--help', :out, :err;
    my $out = $p.out.slurp(:close);
    $p.err.slurp(:close);
    is $p.exitcode, 0, '--help with a proto MAIN exits 0';
    ok $out.starts-with('Usage:'), '…printing the usage';
    $script.unlink;
}
