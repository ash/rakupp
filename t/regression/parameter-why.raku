# Parameter-level declarator docs reach introspection: a param's trailing
# `#= doc` (or leading `#|`) answers .signature.params[N].WHY, as in Rakudo.
# The doc was always captured (Param.pod drives $*USAGE's option list) but
# never plumbed into the reflected Parameter object — .WHY answered Nil.
# Spun off from the v3 CLI step-2 work (the MAIN-usage fixes, issue #17);
# oracle-verified against Rakudo 2026.07. Passes under both engines.

my $fails = 0;
sub check(Str $desc, $got, $want) {
    if $got eq $want {
        say "ok - $desc";
    }
    else {
        $fails++;
        say "not ok - $desc";
        note "GOT [{$got}] WANT [{$want}]";
    }
}

sub g(
    Int $a,  #= a doc
    Str :$b, #= b doc
) { }
check('positional #= doc',      (&g.signature.params[0].WHY // 'Nil').gist, 'a doc');
check('named #= doc',           (&g.signature.params[1].WHY // 'Nil').gist, 'b doc');

sub h(
    #| lead doc
    Int $c,
    Int $d,
) { }
check('leading #| documents the param below it',
      (&h.signature.params[0].WHY // 'Nil').gist, 'lead doc');
check('an undocumented param answers Nil',
      (&h.signature.params[1].WHY // 'Nil').gist, 'Nil');

#| the routine itself
sub k(Int $e) { }
check('the routine doc stays the routine doc', (&k.WHY // 'Nil').gist, 'the routine itself');
check('…and does not leak into its params',
      (&k.signature.params[0].WHY // 'Nil').gist, 'Nil');

say $fails == 0 ?? 'PASS' !! 'FAIL';
exit($fails ?? 1 !! 0);
