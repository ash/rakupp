# Regression: `use nqp` ops must NATIVE-COMPILE (--exe), not fall back to AOT
# bundling. The eager leaf ops share a runtime entry (rtNqpOp) with the
# interpreter; the lazy control forms (while/until/stmts/ifnull) emit native
# C++. Interpreter and compiled binary must agree byte-for-byte. 2026-07-23.

my $ok = True;

my $prog = q:to/NQP/;
use nqp;
print nqp::add_i(20,22), " ";
print nqp::substr("hello world",6,5), " ";
print nqp::concat("a","b"), " ";
my int $i = 0; my int $s = 0;
nqp::while(nqp::islt_i($i,5),
  nqp::stmts(($s = nqp::add_i($s,$i)),($i = nqp::add_i($i,1))));
print $s, " ";
print nqp::if(nqp::iseq_i(1,1),"Y","N"), " ";
print nqp::findnotcclass(nqp::const::CCLASS_NUMERIC,"12ab",0,4);
NQP

my $src = 'temp_nqpc_' ~ $*PID ~ '.raku';
my $exe = 'temp_nqpc_' ~ $*PID ~ '.bin';
spurt $src, $prog;

my $interp = qqx{$*EXECUTABLE $src}.trim;
my $comp   = qqx{$*EXECUTABLE --exe $src -o $exe 2>&1};
my $native = $exe.IO.e ?? qqx{./$exe}.trim !! "(compile failed: $comp)";

unlink $src;
unlink $exe if $exe.IO.e;

unless $interp eq '42 world ab 10 Y 2' {
    say "FAIL: interpreter output wrong: {$interp.raku}";
    $ok = False;
}
unless $native eq $interp {
    say "FAIL: --exe output differs from interpreter";
    say "  interp: {$interp.raku}";
    say "  native: {$native.raku}";
    $ok = False;
}

say 'PASS' if $ok;
