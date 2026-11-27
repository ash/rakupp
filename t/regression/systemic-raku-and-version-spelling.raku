# Regression: `dd $*KERNEL` and the Systemic objects' own faces (#279), and
# Version.new's spelling.
#
# $*KERNEL, $*DISTRO and $*VM answered `.raku`, `.WHAT` and `.HOW` with their name
# (`dd $*KERNEL` printed `darwin`), where Rakudo gives `Kernel.new(name => …,
# version => …, …)` and the type object. `$*KERNEL.desc` was the name too, not
# Rakudo's Str; any other method they lack answered the name too (`.lc`,
# `.elems`, a misspelt `.hostnmae`), where Rakudo dies or answers as Any does.
# Behind the Kernel's version: Version.new kept its string as
# written, where Rakudo spells it as its parts joined by dots
# (`Version.new("1.2-3")` is v1.2.3), and a word part is letters and `_`
# together (`a_b` is one part, `1_01` is 1, `_`, 1).
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($ok, $what) { @fail.push($what) unless $ok }

for $*KERNEL, Kernel, <name version hardware arch bits release desc signature>,
    $*DISTRO, Distro, <release path-sep name auth version signature desc>,
    $*VM,     VM,     <config prefix precomp-ext precomp-target name auth version signature desc>
    -> $obj, $type, @attrs {
    my $n = $type.^name;
    check $obj.WHAT === $type, "$n: .WHAT is the type object";
    check $obj.WHAT.gist eq "($n)", "$n: .WHAT.gist is ($n) (got {$obj.WHAT.gist})";
    check $obj.HOW.^name eq $type.HOW.^name, "$n: .HOW is the type's (got {$obj.HOW.^name})";
    my $r = $obj.raku;
    check $r.starts-with("{$n}.new(") && $r.ends-with(')'), "$n: .raku is a {$n}.new(…) (got {$r.substr(0, 40)})";
    my @seen = $r.match(/ <?after '(' | ', '> (<[\w-]>+) ' => ' /, :g).map(~*[0]);
    check @seen.head(@attrs.elems).List eqv @attrs.List, "$n: .raku lists {@attrs} in Rakudo's order (got {@seen.head(@attrs.elems)})";
    check $obj eq $obj.name, "$n: still stringifies as its name";
}
check $*KERNEL.raku.contains("name => {$*KERNEL.name.raku}"), 'Kernel .raku carries the name';
check $*KERNEL.raku.contains('bits => ' ~ $*KERNEL.bits), 'Kernel .raku carries the bits';
check $*KERNEL.desc === Str, "Kernel .desc is Str (got {$*KERNEL.desc.raku})";
check $*KERNEL.gist eq $*KERNEL.name, 'Kernel .gist is its name';
check $*KERNEL.version ~~ Version, 'Kernel .version is a Version';

# a method none of them has dies, as in Rakudo, where it answered the name
for $*KERNEL, $*DISTRO, $*VM -> $obj {
    my $n = $obj.^name;
    for <hostnmae lc chars Num comb> -> $m {
        my $r = try $obj."$m"();
        check $! ~~ X::Method::NotFound, "$n.$m dies with X::Method::NotFound (got {$! ?? $!.^name !! $r.raku})";
        check $!.message.starts-with("No such method '$m' for invocant of type '$n'"), "$n.$m: Rakudo's message" if $!;
    }
    for <Numeric Int split> -> $m {
        my $r = try $obj."$m"();
        check $! ~~ X::Multi::NoMatch, "$n.$m dies with X::Multi::NoMatch (got {$! ?? $!.^name !! $r.raku})";
    }
    check $obj.defined && $obj.so && $obj.Bool && !$obj.not && $obj.DEFINITE, "$n is a defined, true object";
    check $obj.elems == 1, "$n.elems is 1";
    check $obj.keys.List eqv (0,), "$n.keys is (0,)";
    check $obj.values[0] eq $obj.name, "$n.values is the object itself";
    check $obj.list.elems == 1 && $obj.list[0] eq $obj.name, "$n.list is a list of itself";
    check $obj.WHICH ~~ ObjAt, "$n.WHICH is an ObjAt";
    check $obj.isa($obj.WHAT) && $obj ~~ $obj.WHAT, "$n isa and smartmatches its type";
    check ?$obj.can('name') && ?$obj.can('say') && ?$obj.can('elems') && !$obj.can('lc'), "$n.can knows its methods";
}
check $*KERNEL.auth eq 'unknown', "Kernel .auth is 'unknown' (got {$*KERNEL.auth})";
check $*KERNEL.cpu-cores-but-one == max(1, $*KERNEL.cpu-cores - 1), 'Kernel .cpu-cores-but-one';
check $*DISTRO.cur-sep eq ',', 'Distro .cur-sep is ","';
check $*VM.osname eq $*KERNEL.name, "VM .osname is the kernel's name (got {$*VM.osname})";
check $*VM.remote-debugging == 0, 'VM .remote-debugging is 0';
check $*KERNEL.version.Str !~~ / <-[\w.*+-]> /, "Kernel .version is spelled with dots (got {$*KERNEL.version})";

# Version.new spells the string as Rakudo does
for "1.2-3" => 'v1.2.3', "1a2" => 'v1.a.2', "1..2" => 'v1.2', "1.2." => 'v1.2', ".1" => 'v1',
    "1+2" => 'v1.2', "1.2+" => 'v1.2+', "1.2-" => 'v1.2-', "1.*.3" => 'v1.*.3', "01.2" => 'v01.2',
    "1_2" => 'v1._.2', "a b" => "Version.new('a.b')", "x:1~y" => "Version.new('x.1.y')",
    "v0.0.1" => "Version.new('v.0.0.1')", "a_b" => "Version.new('a_b')", "*" => "Version.new('*')" {
    my $v = Version.new(.key);
    check $v.raku eq .value, "Version.new({.key.raku}).raku is {.value} (got {$v.raku})";
}
check Version.new("a_b").parts eqv ("a_b",), 'a_b is one part';
check Version.new("1.2.1_01").parts eqv (1, 2, 1, "_", 1), '1.2.1_01 is (1 2 1 _ 1)';
check Version.new("RELEASE_ARM64_T8132").parts eqv ("RELEASE_ARM", 64, "_T", 8132), 'RELEASE_ARM64_T8132 splits as Rakudo does';
check (Version.new("a_b") cmp Version.new("a.b")) == More, 'a_b sorts after a.b';
check (Version.new("1.2.1_01") cmp Version.new("1.2.1.1")) == Less, '1.2.1_01 sorts before 1.2.1.1';
check Version.new("1.2-3") eqv v1.2.3, 'Version.new("1.2-3") eqv v1.2.3';
check Version.new("1.2.3") ~~ v1.2+, '1.2.3 ~~ v1.2+';

if @fail { .say for @fail; say 'FAIL'; exit 1 }
say 'PASS';
