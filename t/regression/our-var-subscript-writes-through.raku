# Regression: subscripting a published `our @a` / `our %h` reaches the container,
# not the view standing in for it.
#
# f18b71c published an `our` variable as a VIEW onto the declaring scope's slot —
# a Proxy — so that `$Pkg::VAR = 4` writes the variable the package reads. A
# SUBSCRIPT never looked through it:
#
#   %Pkg::h<k> = 1   the Proxy IS a hash, so the key was filed in among its own
#                    FETCH and STORE entries, where no reader could find it —
#                    not even the line that had just written it.
#   @Pkg::a[1] = 1   an Array was wanted and a Hash found, so the view was
#                    REPLACED by a fresh empty array: element 0 gone, and the
#                    package's own reader still on the container it always had.
#
# `%Cfg::OPT<width> = 120` in front of a module that keeps its settings that way
# is the shape that matters, and it is the last row here.
# Each row gets its own package and its own markers.
my $ok = 0; my $n = 0;
sub ck($got, $want, $desc) {
    $n++;
    if $got eqv $want { $ok++ }
    else { note "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

# 1. a hash key written from outside is the package's own key
class C1 { our %h = k => 'c1-in'; method peek { %h<k> } }
%C1::h<k> = 'c1-out';
ck %C1::h<k>,       'c1-out', 'the writer can read a hash element back';
ck C1.new.peek,     'c1-out', '…and so can the class';

# 2. a NEW key, not just a replaced one
class C2 { our %h = k => 'c2-keep'; method peek { %h.sort.map({ .key ~ '=' ~ .value }).join(',') } }
%C2::h<z> = 'c2-new';
ck C2.new.peek, 'k=c2-keep,z=c2-new', 'a new hash key lands in the same hash';

# 3. an array element written from outside
class C3 { our @a = <c3-in>; method peek { @a[0] } }
@C3::a[0] = 'c3-out';
ck @C3::a[0],   'c3-out', 'the writer can read an array element back';
ck C3.new.peek, 'c3-out', '…and so can the class';

# 4. writing PAST the end must not drop what is already there
class C4 { our @a = <c4-zero>; method peek { @a.join(',') } }
@C4::a[1] = 'c4-one';
ck C4.new.peek, 'c4-zero,c4-one', 'growing the array keeps the elements it had';
ck @C4::a.elems, 2,               '…and no fresh container replaced it';

# 5. the same through a `module` block, which publishes by a different route
module M5 {
    our @a = <m5-in>;
    our %h = k => 'm5-in';
    our sub peek { @a[0] ~ '/' ~ %h<k> }
}
@M5::a[0] = 'm5-out';
%M5::h<k> = 'm5-out';
ck M5::peek(), 'm5-out/m5-out', 'a module block subscript writes through too';

# 6. nested: a hash of hashes reached by two subscripts
class C6 { our %h = outer => { inner => 'c6-in' }; method peek { %h<outer><inner> } }
%C6::h<outer><inner> = 'c6-out';
ck C6.new.peek, 'c6-out', 'a nested subscript writes through as well';

# 7. autovivifying a key that the package never declared
class C7 { our %h; method peek { %h<made-up> // 'unset' } }
%C7::h<made-up> = 'c7-viv';
ck C7.new.peek, 'c7-viv', 'autovivifying through the name reaches the package hash';

# 8. .push and whole-container assignment, which went through another path and
#    must keep working
class C8 { our @a = <c8-first>; method peek { @a.join(',') } }
@C8::a.push: 'c8-pushed';
ck C8.new.peek, 'c8-first,c8-pushed', 'push still reaches the same array';
class C9 { our @a = <c9-old>; method peek { @a.join(',') } }
@C9::a = <c9-new>;
ck C9.new.peek, 'c9-new', 'whole-container assignment still replaces it';

# 9. a plain lexical container is untouched by any of this
my %plain = k => 'plain-in';
my @plainr = <plain-in>;
%plain<k> = 'plain-out';
@plainr[1] = 'plain-two';
ck %plain<k>, 'plain-out', 'a plain hash element still assigns';
ck @plainr.join(','), 'plain-in,plain-two', 'a plain array still grows';

# 10. the shape that matters: a real module, `use`d, its settings written by the
#     program in front of it. `use` is compile time and the module is written as
#     this case runs, so the probe is a child.
my $dir = $*TMPDIR.add("rakupp-our-subscript-{$*PID}");
$dir.mkdir;
$dir.add('OurCfg176.rakumod').spurt: q:to/MOD/;
unit module OurCfg176;
our $TAB = 8;
our %OPT = width => 80;
our @LIST = <alpha>;
sub report() is export { "$TAB/%OPT<width>/@LIST[0]" }
MOD
my $probe = $dir.add('probe.raku');
$probe.spurt: q:to/PROBE/;
use OurCfg176;
$OurCfg176::TAB = 4;
%OurCfg176::OPT<width> = 120;
@OurCfg176::LIST[0] = 'omega';
say "module={report()}";
PROBE
my $p = run $*EXECUTABLE.absolute, '-I' ~ $dir.absolute, $probe.absolute, :out, :err;
my $line = $p.out.slurp(:close).trim.lines.first({ .starts-with('module=') }) // '';
$p.err.slurp(:close);
ck $line, 'module=4/120/omega',
   'a module reads the settings the program wrote through its qualified names';
try { .unlink for $dir.dir; $dir.rmdir }

say $ok == $n ?? 'PASS' !! "FAIL ($ok/$n)";
exit $ok == $n ?? 0 !! 1;
