# `nqp::istype($dt, Dateish)` answered False while `$dt ~~ Dateish` answered
# True — the same question down two different paths, disagreeing. rtTypeMatch
# (nqp::istype and native multi-dispatch) matched a tagged built-in against its
# OWN NAME only, never the role it does, while `~~` reads typeNameConforms,
# which has had Date/DateTime -> Dateish all along.
#
# It stayed invisible because a hash-backed DateTime also counted as
# Associative, and JSON::Fast's jsonify tests Associative BEFORE Dateish — so
# DateTimes were serialized down a branch that was wrong for a reason that
# happened to produce the right bytes. Making Associative correct (a
# hash-BACKED value is not thereby Associative) took the cover away and
# JSON::Fast's t/07-datetime.t went red with "Don't know how to jsonify
# DateTime". Both halves are asserted here so neither can drift back.
use nqp;
my $fail = 0;
sub check($ok, $what) { $fail++ unless $ok; say ($ok ?? "ok   " !! "FAIL ") ~ $what }

my $dt = DateTime.now;
my $d  = Date.today;

# the two paths must agree, and both must say True (as Rakudo does)
check($dt ~~ Dateish,                          'DateTime ~~ Dateish');
check(?nqp::istype($dt, Dateish),              'nqp::istype(DateTime, Dateish)');
check($d ~~ Dateish,                           'Date ~~ Dateish');
check(?nqp::istype($d, Dateish),               'nqp::istype(Date, Dateish)');

# and the fix that exposed it must stay fixed: hash-BACKED is not Associative
check(!($dt ~~ Associative),                   'DateTime is NOT Associative');
check(!($d  ~~ Associative),                   'Date is NOT Associative');
check(!nqp::istype($dt, Associative),          'nqp::istype(DateTime, Associative) is False');

# a real Hash still is, both ways
my %h = a => 1;
check(%h ~~ Associative,                       'a Hash still smartmatches Associative');
check(?nqp::istype(%h, Associative),           'nqp::istype(Hash, Associative)');

# the tag's own name still answers
check(?nqp::istype($dt, DateTime),             'nqp::istype(DateTime, DateTime)');
check(!nqp::istype($dt, Date),                 'a DateTime is not a Date');

say $fail == 0 ?? "PASS" !! "FAIL ($fail)";
