# Regression: the 2026-09-14 ecosystem-blocker batch — the engine faults between
# rakupp and four dists that stand in front of others (IO::MiddleMan, Lumberjack,
# InterceptAllMethods, FatRatStr), found by running `rakupp test` over the
# dependency blockers the 2026-09-12 sweep names.
#
#   * `IO::Handle.new` — an UNOPENED handle (and its `.path` is not the handle)
#   * `.print-nl` writes the handle's output separator
#   * `$fh.say($obj)` renders with the object's OWN `method gist`
#   * an enum's value list is a TERM, and a trait may follow it:
#     `enum Level <Off Fatal Error> does role { … }`
#   * a backtrace frame is `Backtrace::Frame`, a name a signature can write
#   * `constant class = Foo` — a declarator keyword is a perfectly good NAME
#   * a DELEGATED `Numeric` (`has FatRat $.f handles <Numeric>`) numifies for
#     the numeric comparisons
#
# Runs clean under Rakudo too.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

# ---- an unopened handle -------------------------------------------------
ck IO::Handle.new.defined, True, '`IO::Handle.new` is a handle, not a failure';
# (its `.path` is not comparable across engines: Rakudo answers an IO that
# throws when asked anything, this engine an IO::Path type object. What matters
# is that neither hands back the HANDLE stringified, which is what it used to.)

# ---- `.print-nl` and `.say` through a handle ----------------------------
class Gisty { method gist { 'gist works!' } }
my $tmp = $*TMPDIR.add("rakupp-handle-probe-{$*PID}.txt");
{
    my $fh = $tmp.open(:w);
    $fh.print('a');
    $fh.print-nl;
    $fh.say(Gisty.new);
    $fh.close;
}
ck $tmp.slurp, "a\ngist works!\n",
   '`.print-nl` writes the separator and `.say` uses the object`s own gist';
$tmp.unlink;

# ---- an enum's value list is a TERM, and traits follow it ---------------
enum LogLevel <LogOff LogFatal LogError LogWarn> does role {
    multi method ACCEPTS($m) { True }
};
ck LogLevel.enums.keys.sort.join(','), 'LogError,LogFatal,LogOff,LogWarn',
   'an enum with a trailing `does role` still has its members';
ck LogError.key, 'LogError', '…and each of them is a value of its own';
ck LogError.value, 2,        '…numbered in order';

# ---- a backtrace frame can be named ------------------------------------
ck (Backtrace.new.list.head ~~ Backtrace::Frame), True,
   'a backtrace frame IS a Backtrace::Frame';

# ---- a declarator keyword is a name ------------------------------------
class Intercepted { method who { 'intercepted' } }
constant class = Intercepted;
ck class.who, 'intercepted', '`constant class = …` declares a constant named class';

# ---- a DELEGATED Numeric numifies --------------------------------------
class Wrapped { has FatRat $.f handles <Numeric Num Int Real abs nude> }
my $w = Wrapped.new: :f(FatRat.new(1, 4));
ck ($w == 0.25),  True,  'a delegated `Numeric` decides `==`';
ck ($w < 0.5),    True,  '…and `<`';
ck ($w <=> 0.25), Order::Same, '…and `<=>`';

exit $fails ?? 1 !! 0;
