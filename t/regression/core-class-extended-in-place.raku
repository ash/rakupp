# Regression: extending a CORE class in place, the way DateTime::Timezones
# does, failed at each of its steps.
#
#   - `CORE::DateTime` named no type a method could be called on
#     ("No such method 'new' for invocant of type 'CORE::DateTime'"), and a
#     multi's `CORE::DateTime $self` candidate matched nothing;
#   - `unit module Timezones; class DateTime is DateTime is export` — a
#     subclass that shadows the core class — was refused as inheriting from
#     itself, where Rakudo reads the parent as what the name meant before;
#   - `DateTime.^find_method('in-timezone').wrap(…)` was accepted and IGNORED:
#     the method object is made on the spot and dispatch is C++;
#   - a method `.^add_fallback` computes, and `$d.&sub`, got the invocant's
#     VALUE, so `sub up($s is rw)` could not upgrade `$d`, and a multi took
#     its `is rw` candidate for a value (`my \d = DateTime.new(…)`, `3.&m`);
#   - a subclassed DateTime's `-`/`+` numified it (`$dt - $day` was a Num),
#     and its `.later`, `.in-timezone`, `.clone` … answered a plain DateTime.
# Every answer below is Rakudo 2026.09's.

my $fails = 0;
sub ok($cond, $what) { $fails++ unless $cond; say "not ok - $what" unless $cond }

# ---- CORE:: names the setting's type ----
ok CORE::DateTime.new(0).year == 1970, 'CORE::DateTime.new';
ok CORE::DateTime =:= DateTime, 'CORE::DateTime is DateTime';
ok CORE::X::AdHoc.new(:payload<p>).message eq 'p', 'CORE::X::AdHoc';
multi sub kind(CORE::DateTime $d) { 'core' }
multi sub kind(Str $s) { 'str' }
ok kind(DateTime.new(0)) eq 'core', 'a CORE::DateTime candidate matches a DateTime';

# ---- a module's class shadows the core one and inherits it ----
my $dir = $*TMPDIR.add("rakupp-core-ext-{$*PID}");
$dir.add('lib').mkdir;
LEAVE { run 'rm', '-rf', $dir.Str }
$dir.add('lib/Shadow.rakumod').spurt: q:to/MOD/;
    unit module Shadow;
    class DateTime is DateTime is export {
        has $.extra = 42;
        method hi { 'hi from ' ~ ::?CLASS.^name }
        method core { CORE::DateTime.new(self.Instant).^name }
    }
    MOD
my $p = run $*EXECUTABLE, '-I', $dir.add('lib').Str, '-e',
            'use Shadow; my $d = DateTime.new(0); print join "|", DateTime.^mro.map(*.^name).join(","), $d.hi, $d.extra, $d.year, $d.core',
            :out, :err;
my $out = $p.out.slurp(:close); $p.err.slurp(:close);
ok $out eq 'Shadow::DateTime,DateTime,Any,Mu|hi from Shadow::DateTime|42|1970|DateTime',
   "class DateTime is DateTime in a module (got '$out')";
ok (try EVAL 'class A is A { }; 1') === Nil, '…while `class A is A` still refuses';
# …and one OUTSIDE a package of the core class's own name works (Rakudo) or is
# refused as not yet implemented (here) — it crashed, `is CORE::DateTime`
my $q = run $*EXECUTABLE, '-e', 'class DateTime is CORE::DateTime { method hi { "hi" } }; print DateTime.new(0).hi',
            :out, :err;
my $qo = $q.out.slurp(:close); my $qe = $q.err.slurp(:close);
ok $q.signal == 0 && ($qo eq 'hi' || ($q.exitcode != 0 && $qe.contains('X::NYI'))),
   "a top-level core namesake works or is refused, never crashes (exit {$q.exitcode}, signal {$q.signal})";

# ---- .wrap of a core method ----
my $h = DateTime.^find_method('in-timezone').wrap(method (|c) { 'wrapped:' ~ callsame });
ok DateTime.new(0).in-timezone(3600) eq 'wrapped:1970-01-01T01:00:00+01:00', 'the wrapper runs, callsame reaches the core method';
$h.restore;
ok DateTime.new(0).in-timezone(3600) eq '1970-01-01T01:00:00+01:00', '.restore takes it off';
class MyDT is DateTime { has $.x = 5 }
my $h2 = MyDT.^find_method('hour').wrap(method (|c) { 99 });
ok DateTime.new(0).hour == 99 && MyDT.new(0).hour == 99, "a subclass's find_method is the core method, wrapped for all";
$h2.restore;

# ---- the invocant's container ----
proto sub up(|) {*}
multi sub up(CORE::DateTime $s is rw) { $s = 5; 'rw' }
multi sub up(CORE::DateTime $s) { 'ro' }
DateTime.^add_fallback(anon sub c($o, $w) { $w eq 'up' }, anon sub k($o, $w) { &up });
my $d = DateTime.new(0);
ok $d.up eq 'rw' && $d == 5, 'a fallback sub takes the variable, `is rw`';
my \sd = DateTime.new(0);
ok sd.up eq 'ro', 'a sigilless name bound to a value takes the other candidate';
ok DateTime.new(0).up eq 'ro', '…and so does a constructor call';
sub bump($s is rw) { $s = 7 }
my $n = 1; $n.&bump;
ok $n == 7, '$x.&sub takes the container too';
proto sub rw-or-not(|) {*}
multi sub rw-or-not($s is rw) { 'rw' }
multi sub rw-or-not($s) { 'ro' }
ok 3.&rw-or-not eq 'ro' && $n.&rw-or-not eq 'rw', 'a literal is no container for .&sub, a variable is';

# ---- a subclassed DateTime keeps its class through arithmetic and methods ----
my $m = MyDT.new(:2022year, :6month, :15day, :4hour, :x(7));
my $day = Duration.new(86400);
ok ($m - $day).^name eq 'MyDT' && ($m - $day).day == 14 && ~($m - $day) eq '2022-06-14T04:00:00Z',
   'subclass - Duration';
ok ($day + $m).^name eq 'MyDT' && ~($m + $day) eq '2022-06-16T04:00:00Z', 'Duration + subclass';
my $e = MyDT.new(:2022year, :6month, :15day, :3hour);
ok ($m - $e).^name eq 'Duration' && $m - $e == 3600 && $m > $e, 'a difference is a Duration';
ok ~(MyDT.new(0) + 60) eq 'Instant:70', 'a plain number meets the Instant';
ok $m.in-timezone(3600).^name eq 'MyDT' && $m.in-timezone(3600).x == 7, '.in-timezone keeps class and attributes';
ok $m.later(:1day).^name eq 'MyDT' && !$m.later(:1day).x.defined, '.later keeps the class only';

say $fails == 0 ?? 'PASS' !! "FAIL ($fails)";
exit $fails == 0 ?? 0 !! 1;
