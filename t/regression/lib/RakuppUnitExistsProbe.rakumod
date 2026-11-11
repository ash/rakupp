# For t/regression/unit-stash-exists-under-import-list.raku: an EXPORT that
# exports, beside what it is asked for, a sub answering what UNIT:: said
# about the file's own symbols — `&uep-report-list` with an import list,
# `&uep-report-bare` without.
my sub rakupp-uep-greet() is export { "hello" }
my sub EXPORT(*@names) {
    my $k = '&rakupp-uep-greet';
    my %r = 
        names   => @names.List,
        plain   => UNIT::{$k}.defined,
        exists  => (UNIT::{$k}:exists),
        angle   => (UNIT::<&rakupp-uep-greet>:exists),
        pair    => (UNIT::{$k}:p).elems,
        export  => (UNIT::{'&EXPORT'}:exists),
        missing => (UNIT::{'&no-such-sub'}:exists),
        keys    => UNIT::.keys.grep(*.starts-with('&rakupp-uep')).List;
    Map.new: ('&uep-report-' ~ (@names ?? 'list' !! 'bare')) => sub { %r }
}
