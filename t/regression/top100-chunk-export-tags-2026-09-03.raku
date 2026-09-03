# Regression: selective export tags. `is export(:foo)` is published only when the
# importer asks for the tag (`use Mod :foo`); a plain `is export`, `:DEFAULT` and
# `:MANDATORY` are published on a bare `use`. Before this, rakupp published EVERY
# `is export(:tag)` sub on a bare `use`, so `nok MY::<&prompt>:exists` after
# `use Prompt` failed (Prompt's t/01-basic). Rakudo 2026.08 is the oracle; this
# file drives a fixture module through fresh processes so each scenario is clean.
#
# (rakupp is deliberately MORE lenient than Rakudo in one respect: `use Mod :tag`
# also keeps the default exports, where Rakudo imports only the tag group —
# withholding a default that already works is the bigger risk, so this only ADDS
# the selective sub. The assertions below are the behaviours BOTH engines share.)
my $fails = 0;
sub ok($cond, $what) { $fails++ unless $cond; note "not ok - $what" unless $cond }

my $dir = $*TMPDIR.add("rakupp-exptags-{$*PID}-{(^100000).pick}");
$dir.add('lib').mkdir;
$dir.add('lib/ExpTags.rakumod').spurt(q:to/MOD/);
    unit module ExpTags;
    sub plain     is export                    { "plain" }
    sub mand      is export(:MANDATORY)        { "mand" }
    sub deflt     is export(:DEFAULT)          { "deflt" }
    sub combo     is export(:DEFAULT, :extra)  { "combo" }
    sub selective is export(:sel)              { "selective" }
    MOD

my $libarg = $dir.add('lib').Str;
sub probe($body) {
    my $f = $dir.add("p-{(^1000000).pick}.raku");
    $f.spurt("use lib '$libarg';\n" ~ $body);
    my $p = run($*EXECUTABLE, $f.Str, :out, :err);
    my $o = $p.out.slurp(:close).trim; $p.err.slurp(:close);
    try $f.unlink;
    $o;
}

# A plain `use`: default / MANDATORY / DEFAULT / combo export; a selective tag does not.
ok(probe('use ExpTags; print (MY::<&plain>:exists, MY::<&mand>:exists, MY::<&deflt>:exists, MY::<&combo>:exists).join(",")')
   eq 'True,True,True,True',
   'a plain use exports the default, MANDATORY, DEFAULT and combo(:DEFAULT,...) subs');
ok(probe('use ExpTags; print MY::<&selective>:exists') eq 'False',
   'a plain use does NOT export a selective is export(:sel) sub');

# `use Mod :sel` imports the selective sub.
ok(probe('use ExpTags :sel; print MY::<&selective>:exists') eq 'True',
   'use Mod :sel imports the selective sub');

# …and a repeat `use Mod :sel` (module body already ran) still imports it.
ok(probe('use ExpTags; { use ExpTags :sel; print MY::<&selective>:exists }') eq 'True',
   'a repeat use Mod :sel imports the selective sub even after a plain use');

try { .unlink for $dir.add('lib').dir(test => *.ends-with('.rakumod')); }
try { $dir.add('lib').rmdir; $dir.rmdir; }
say $fails ?? "FAIL ($fails)" !! "PASS";
