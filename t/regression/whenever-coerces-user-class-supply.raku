# Regression: `whenever $obj` ran its body once with the object as the value
# when $obj's class has a Supply method of its own; Rakudo taps that Supply.
# IO::Socket::Async::SSL's upgrade test reads a line with `whenever
# $enc-conn.head` (`.head` of one object is that object), and got
# 'IO::Socket::Async::SSL<…>' where it wanted "HELLO!\n". The coercion was
# held back while it made that test hang; with it the test now passes and
# does not hang.
#
# Expectations checked against Rakudo 2026.09
# (~/.rakubrew/versions/moar-2026.09/install/bin/rakudo). Green on both.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

my class Publisher { method Supply { Supply.from-list(<a b>) } }
my class Plain { }

{
    my @got;
    react { whenever Publisher.new -> $v { @got.push($v) } }
    ck @got, ['a', 'b'], "whenever taps a user class's Supply";
}

{
    my @got;
    react { whenever Publisher.new.head -> $v { @got.push($v) } }
    ck @got, ['a', 'b'], '…and so does the .head of one such object';
}

{
    my @got;
    my $tap = (supply { whenever Publisher.new -> $v { emit $v } }).tap({ @got.push($_) });
    ck @got, ['a', 'b'], '…in a supply block too';
}

{
    my @got;
    react { whenever Plain.new -> $v { @got.push($v.^name) } }
    ck @got, ['Plain'], 'an object with no Supply of its own is its one value';
}

say $fails ?? "FAIL ($fails)" !! 'PASS';
exit $fails ?? 1 !! 0;
