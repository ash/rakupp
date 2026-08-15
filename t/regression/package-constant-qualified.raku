# Regression: a `constant` declared in a package is reachable by its QUALIFIED
# name from outside it. Constants are not `our`, but Rakudo installs them in the
# package's symbol table all the same — and modules rely on it:
# Base64::Native's own suite opens with
#
#     constant Lib = Base64::Native::BASE64-LIB;
#
# to find the library it just built, and Compress::Zlib::Raw's suite reads
# `Compress::Zlib::Raw::Z_OK`. We published nothing, so both died "Undeclared
# name". Classes, grammars and enums declared in a package were already
# reachable that way; only constants were missing.
# Contract: exit 0 + last line PASS.
my @fail;

my $dir = $*TMPDIR.add("pkgconst-$*PID");
mkdir $dir;
mkdir $dir.add('P');
$dir.add('P/Q.rakumod').spurt(q:to/MOD/);
    unit module P::Q;
    constant PLAIN = 'vp';
    constant KEBAB-NAME = 'vk';        # a hyphen in the name is ordinary
    our constant OURS = 'vo';
    constant @LIST = 1, 2, 3;
    constant %MAP = a => 1;
    grammar Gram { token TOP { \w+ } }  # these already worked; keep them working
    class Klass { method hi { 'hi' } }
    enum Level <low high>;
    MOD

my $prog = $dir.add('use.raku');
$prog.spurt(qq:to/PROG/);
    use lib '{$dir}';
    use P::Q;
    say P::Q::PLAIN;
    say P::Q::KEBAB-NAME;
    say P::Q::OURS;
    say \@P::Q::LIST.join(',');
    say \%P::Q::MAP<a>;
    say P::Q::Gram.parse('abc') ?? 'gram' !! 'no';
    say P::Q::Klass.new.hi;
    say P::Q::Level::low;
    PROG

my $p = run($*EXECUTABLE, $prog.Str, :out, :err);
my $out = $p.out.slurp(:close);
my $err = $p.err.slurp(:close);
my @got = $out.lines;
my @want = <vp vk vo 1,2,3 1 gram hi low>;

for @want.kv -> $i, $w {
    @fail.push("line {$i + 1}: got {(@got[$i] // '<missing>').raku}, want {$w.raku}")
        unless (@got[$i] // '') eq $w;
}
@fail.push("stderr: $err.lines.head()") if $err.trim;

unlink $prog, $dir.add('P/Q.rakumod');
rmdir $dir.add('P'); rmdir $dir;

if @fail { note "FAILED:\n" ~ @fail.join("\n"); say 'FAIL' } else { say 'PASS' }
