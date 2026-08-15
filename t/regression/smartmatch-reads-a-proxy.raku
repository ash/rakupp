# Regression: a PROXY on either side of a smartmatch is being READ — run its FETCH, or
#    the match sees the container rather than the value it stands for. Every
#    branch of `~~` keys on the value's TYPE, and a Proxy is internally a Hash,
#    so an `is rw` accessor returning one made the ACCEPTS hook (which wants an
#    Object) silently not fire and the match answer False. Tinky's Object role
#    is `method state(…) is rw { Proxy.new(…) }`, so `self ~~ $object.state`
#    lost — while `self.ACCEPTS($object.state)` and
#    `my $t = $object.state; self ~~ $t` both worked, since assigning to the
#    temp read through the Proxy.
# Contract: exit 0 + last line PASS.
my @fail;
sub ok($desc, $got, $want = True) { @fail.push("$desc: got {$got.raku}, want {$want.raku}") unless $got eqv $want }

# ---- 1. a Proxy is read on both sides of ~~ --------------------------------
class Matcher {
    has $.n;
    multi method ACCEPTS(Matcher:D $o --> Bool) { $!n == $o.n }
}
class Holder {
    has Matcher $!m;
    submethod BUILD(:$m) { $!m = $m }
    method m() is rw {
        Proxy.new(FETCH => sub ($) { $!m }, STORE => sub ($, $v) { $!m = $v })
    }
}
my $one = Matcher.new(n => 1);
my $two = Matcher.new(n => 2);
my $h   = Holder.new(m => $one);

ok('proxy on the RHS',   so ($one ~~ $h.m));
ok('proxy RHS mismatch', so ($two ~~ $h.m), False);
ok('proxy on the LHS',   so ($h.m ~~ $one));
ok('both sides proxy',   so ($h.m ~~ $h.m));
# the plain forms still behave
ok('no proxy',           so ($one ~~ $one));
ok('via a temp',         do { my $t = $h.m; so ($one ~~ $t) });
# and a Proxy still reads as its value elsewhere
ok('proxy value',        $h.m.n, 1);

if @fail { note "FAILED:\n" ~ @fail.join("\n"); say 'FAIL' } else { say 'PASS' }
