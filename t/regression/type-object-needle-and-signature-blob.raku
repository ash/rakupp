# Regression: a type object as the needle of a string predicate answered True,
# and `$*VM.signature` / `$*DISTRO.signature` were Str, not Blob.
#
#   42.ends-with(Str)     — was True: the type object stringified to "" and every
#                           string predicate (index, rindex, contains, starts-with,
#                           ends-with) accepted the empty needle. Rakudo has no
#                           candidate for (Cool:D: Str:U) and dies. Found by the
#                           2026-09-17 Roast ceiling board: S32-str/index.t,
#                           starts-with.t and ends-with.t each lost exactly this
#                           one assertion ("… with wrong args does not hang").
#   $*VM.signature        — was the empty Str; S02-magicals/VM.t and DISTRO.t
#                           assert `isa-ok …, Blob`.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub threw(&c, $what) {
    my $r = try { c() };
    @fail.push("$what: returned {$r.raku} instead of throwing") unless $!;
}
for <index rindex contains starts-with ends-with substr-eq> -> $m {
    threw { 42."$m"(Str) },    "42.$m\(Str)";
    threw { "abc"."$m"(Int) }, "'abc'.$m\(Int)";
}
# the same predicates with a real needle are untouched
@fail.push("index")       unless "abc".index("b") == 1;
@fail.push("rindex")      unless "abcb".rindex("b") == 3;
@fail.push("contains")    unless "abc".contains("b");
@fail.push("starts-with") unless "abc".starts-with("a");
@fail.push("ends-with")   unless "abc".ends-with("c");
@fail.push("substr-eq")   unless "abc".substr-eq("bc", 1);
@fail.push("VM.signature: {$*VM.signature.^name}")       unless $*VM.signature ~~ Blob;
@fail.push("DISTRO.signature: {$*DISTRO.signature.^name}") unless $*DISTRO.signature ~~ Blob;
.say for @fail;
say @fail ?? "FAIL" !! "PASS";
exit +?@fail;
