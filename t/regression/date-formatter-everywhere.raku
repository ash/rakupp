# BROKE: a Date/DateTime built with `:formatter(&code)` rendered through that
# block only for `.Str`/`.gist`, because the block was applied in the method
# dispatcher while the VALUE MODEL (Value::toStr) still wrote ISO 8601. So
# `$d eq '20151229'` was False, `~$d` and `sprintf "%s"` gave the ISO form,
# and Test's `is` — which compares there — read the ISO form too
# (S32-temporal/Date.t 'formatter with y,m,d' / 'with "yyyy-mm-dd"').
# And every DERIVED Date dropped the formatter: makeDate() built a fresh hash
# without it, so .succ/.pred, ±, .later/.earlier, .truncated-to,
# .first-date-in-month/.last-date-in-month and Range iteration all reverted to
# ISO (Date.t "make sure we didn't lose the formatter", "did we keep formatter").
# FIXED: one renderer — toStr applies a stored formatter through the
# g_dateFormat hook and .Str/.gist go through it — and makeDate inherits the
# formatter from the Date a new one is derived from.
my $fmt = { sprintf "%04d%02d%02d", .year, .month, .day };
my $d = Date.new(2015, 12, 29, :formatter($fmt));

# every string coercion, not just .Str
die 'say/interpolation' unless "$d" eq '20151229';
die 'eq'                unless $d eq '20151229';
die '~'                 unless ~$d eq '20151229';
die '.Str'              unless $d.Str eq '20151229';
die '.gist'             unless $d.gist eq '20151229';
die 'sprintf %s'        unless sprintf('%s', $d) eq '20151229';
die 'list gist'         unless ($d, $d).gist eq '(20151229 20151229)';
die 'DateTime eq'       unless DateTime.new(2015,12,29,1,2,3, :formatter({ 'DTF' })) eq 'DTF';

# …but the ISO-form accessors are ISO by name, and .raku is unformatted
die '.yyyy-mm-dd'       unless $d.yyyy-mm-dd eq '2015-12-29';
die 'DT .yyyy-mm-dd'    unless DateTime.new(2015,12,29,1,2,3, :formatter({ 'DTF' })).yyyy-mm-dd eq '2015-12-29';
die 'DT .hh-mm-ss'      unless DateTime.new(2015,12,29,1,2,3, :formatter({ 'DTF' })).hh-mm-ss eq '01:02:03';
die '.raku'             unless $d.raku eq 'Date.new(2015,12,29)';

# a derived Date keeps it
die '.succ'             unless $d.succ eq '20151230';
die '.pred'             unless $d.pred eq '20151228';
die '+ 1'               unless ($d + 1) eq '20151230';
die '- 1'               unless ($d - 1) eq '20151228';
die '.later'            unless $d.later(:1day) eq '20151230';
die '.earlier'          unless $d.earlier(:1day) eq '20151228';
die '.truncated-to'     unless $d.truncated-to('month') eq '20151201';
die '.first-date-in-month' unless $d.first-date-in-month eq '20151201';
die '.last-date-in-month'  unless $d.last-date-in-month eq '20151231';
die '.clone'            unless $d.clone eq '20151229';
die 'Range iteration'   unless (Date.new('2019-05-01', formatter => $fmt)
                                .. Date.new('2019-05-03', formatter => $fmt)).join(' ')
                               eq '20190501 20190502 20190503';
# `$d.Date` on a Date is self and keeps it; `$dt.Date` builds a fresh Date
die 'Date.Date'         unless $d.Date eq '20151229';
die 'DateTime.Date'     unless DateTime.new(2015,12,29,1,2,3, :formatter({ 'DTF' })).Date eq '2015-12-29';

# a formatter changes how a Date PRINTS, never which day it IS
die '==='               unless $d === Date.new(2015, 12, 29);
die 'eqv'               unless $d eqv Date.new(2015, 12, 29);
die 'is-deeply shape'   unless $d.first-date-in-month eqv Date.new(2015, 12, 1);
die 'WHICH'             unless $d.WHICH eq Date.new(2015, 12, 29).WHICH;
die 'one set element'   unless set($d, Date.new(2015, 12, 29)).elems == 1;
# a DateTime identifies by its RENDERING on both engines, so the formatter counts
die 'DateTime ==='      if DateTime.new(2015,12,29,1,2,3, :formatter({ 'DTF' }))
                            === DateTime.new(2015,12,29,1,2,3);
die 'DateTime eqv'      unless DateTime.new(2015,12,29,1,2,3, :formatter({ 'DTF' }))
                               eqv DateTime.new(2015,12,29,1,2,3);
# whichOf is identity for every value kind, and identity carries the type
die 'set !=== bag'      if set(1, 2) === bag(1, 2);
die 'set !=== SetHash'  if set(1, 2) === SetHash.new(1, 2);

# a formatter that renders ANOTHER Date is legitimate and must work
die 'nested formatter'  unless Date.new(2020, 1, 1,
    :formatter({ .year ~ '/' ~ Date.new(2021, 2, 3).Str })).Str eq '2020/2021-02-03';
say 'PASS';
