# Regression: a `flatmap` block that answers a `gather` spread NOTHING:
# `(1, 2).flatmap({ gather { take $_ } })` was `()`. flatmap read the
# block's result through its element buffer, and a gather nothing has
# pulled from yet has an empty one. Getopt::Long builds its receivers as
# `@!options.flatmap(&to-receivers)`, where a Bool option's come from
# `gather for @names { take … }`, so every flag was "Unknown option"
# (issue #235).
# Every line answers the same under Rakudo 2026.09.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) {
    @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want
}

sub g($x) { gather { take $x; take "no$x" } }
check (1, 2).flatmap(&g).List, (1, 'no1', 2, 'no2'), 'a sub answering a gather';
check (1, 2).flatmap({ gather { take $_ } }).List, (1, 2), 'a block';
check (1, 2).flatmap(-> $x { gather for ^2 { take "$x$_" } }).List, ('10', '11', '20', '21'), 'gather for';
check (1, 2).flatmap({ (gather { take $_; take $_ * 10 }).cache }).List, (1, 10, 2, 20), 'a cached gather';
check (1, 2).flatmap({ gather { take $_ for 1..* } })[^3].List, (1, 2, 3), 'an endless one';

# the Getopt::Long shape: receivers by option name, a flag's from a gather
class Flag { }
class Valued { }
multi receivers(Flag, @names) {
    gather for @names -> $name {
        take $name => 0;
        take "no-$name" => 0;
    }
}
multi receivers(Valued, @names) { @names.map: { $^name => 1 } }
my %receivers = ((Valued, <foo f>), (Flag, ['a']), (Flag, ['version'])).flatmap({ receivers(.[0], .[1]) });
check %receivers.keys.sort.List, <a f foo no-a no-version version>, 'flags are among the receivers';

if @fail {
    .say for @fail;
    say "FAIL";
    exit 1;
}
say "PASS";
