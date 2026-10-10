# Regression, 2026-10-10: three module-battery failures (TODO.md "Battery
# regressions since v5.0.1"), each reduced from its dist's own suite and
# answered as Rakudo 2026.09 answers it:
#   - Color t/04-new-invalid: a class's own `proto method new(|) {*}`
#     REPLACES the default constructor, so an all-named call no candidate
#     takes is X::Multi::NoMatch. The proto was dropped when the first
#     `multi method new` made the dispatcher, and the dispatcher then counted
#     Mu.new among the candidates: `Color.new(rgb => [22, 42])` built black.
#   - Encode t/01-basic: `my class X::Encode::Unknown` inside `unit module
#     Encode` lives in the GLOBAL X package's stash, so the short name works
#     from outside; its `.^name` stays the long one.
#   - Trap t/02-tee: a `:w` handle writes at its OWN position. Its descriptor
#     was opened O_APPEND, so after another writer rewrote the file the
#     handle's line landed after it instead of over it.
use Test;
plan 9;

class C {
    proto method new(|) {*}
    multi method new(Real:D :$r, Real:D :$g, *%c) { "rg" }
    multi method new(Array() :$rgb where .defined && $_ ~~ [Real, Real, Real], *%c) { "rgb" }
}
throws-like { C.new(rgb => [22, 42]) }, X::Multi::NoMatch, 'own proto: nothing matching dies';
is C.new(rgb => [22, 42, 1]), 'rgb', '…a matching candidate still runs';
class D { multi method new(Int:D $x) { "int" } }
is D.new(q => 1).^name, 'D', 'no proto: the multis only add to Mu.new';

module Enc {
    my class X::Enc::Unknown is Exception { method message { "unknown" } }
    our sub boom { X::Enc::Unknown.new.throw }
}
throws-like { Enc::boom() }, X::Enc::Unknown, message => 'unknown', 'my class X::… is seen by its short name';
is X::Enc::Unknown.^name, 'Enc::X::Enc::Unknown', '…and keeps its long name';

my $f = $*TMPDIR.add("rakupp-wpos-{$*PID}.txt");
LEAVE try $f.unlink;
my $h = $f.open(:w);
$f.spurt("AAAA\n");
$h.print("BB\n");
$h.close;
is $f.slurp, "BB\nA\n", 'a :w handle writes at its position, over a later writer';
my $a = $f.open(:a); $a.print("Z"); $a.close;
is $f.slurp, "BB\nA\nZ", 'a :a handle still appends';
my $u = $f.open(:w, :enc<utf16>); $u.print("hi"); $u.close;
is $f.slurp(:bin).list.head(2), (0xFF, 0xFE), 'a utf16 :w handle starts with its BOM';
is $f.slurp(:bin).elems, 6, '…and the text follows it';
