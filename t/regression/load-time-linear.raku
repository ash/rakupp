# Regression: loading a source stays linear in its length (issue #141).
#
# 5.3.0 lexed every word that follows a term by asking the WHOLE source whether
# it spells `infix:<that-word>` (be813251), so `1 if $x > 3` — and the `sub`
# after each closing `}` — re-scanned the file once per statement: 3000
# one-line subs went from 0.04 s to 0.19 s, 30000 took 16 s. The words are now
# collected once, up front. The last case is older (5.2.1 has it too): a `%`
# after a name (`my %h`) asked whether a `/` followed on the same line by
# searching for the next slash ANYWHERE in the file, so a file without one was
# scanned to its end per hash variable. The search now stops at the newline.
#
# Each case is EVAL'd at 2k and at 16k statements, best of three: eight times
# the source takes about eight times as long when loading is linear and about
# sixty-four when it is quadratic. A case fails only when its growth is past
# 24x AND the long run took 100 ms or more, so load on a busy machine slows
# both sizes without turning a linear case red. The output carries no timings.

use MONKEY-SEE-NO-EVAL;

my $fails = 0;
sub ck($ok, $desc) {
    if $ok { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc" }
}

my @cases =
    'postfix if after a term'  => -> $f, $i { "sub $f\() \{ 1 if $i > 3; 2 }\n" },
    'postfix with, until'      => -> $f, $i { "sub $f\() \{ 1 with $i; 2 until True; 3 }\n" },
    'word infix after a term'  => -> $f, $i { "sub $f\() \{ $i div 2 + $i mod 3 }\n" },
    'block if after a brace'   => -> $f, $i { "sub $f\() \{ if $i > 3 \{ 1 } else \{ 2 } }\n" },
    'my %h after a name'       => -> $f, $i { "sub $f\() \{ my \%h; my \%g; my \%k; $i }\n" };

my $run = 0;
sub ms(&stmt, $n) {
    my $best = Inf;
    for ^3 {
        my $src = (^$n).map({ &stmt("f{$run}_$_", $_) }).join ~ "1;\n";
        $run++;
        my $t = now;
        EVAL $src;
        $best min= (now - $t) * 1000;
    }
    $best
}

for @cases -> $c {
    my $name = $c.key; my &stmt = $c.value;
    my $short = ms(&stmt, 2_000);
    my $long  = ms(&stmt, 16_000);
    my $quadratic = $long >= 100 && $long / max($short, 0.01) >= 24;
    ck(!$quadratic, "$name: linear");
    note "  $name: {$short.fmt('%.1f')} ms at 2k, {$long.fmt('%.1f')} ms at 16k" if $quadratic;
}

say $fails ?? "FAILED $fails" !! "PASS";
