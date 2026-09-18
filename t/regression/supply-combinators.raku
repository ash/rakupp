# Regression: the Supply combinators — what each one emits, and when.
# Section D of docs/dev/findings/semantics/Supply.md (S-27 to S-48), extracted
# from Rakudo 2026.08 and implemented here from the sheet.
#
# What was wrong before: `reduce` emitted nothing for an empty source; `migrate`
# passed plain values through instead of demanding Supplies; `classify` and
# `categorize` returned one Hash instead of `key => Supply` Pairs, and collapsed
# 1 with "1"; `comb` joined pieces across chunk boundaries and ignored its
# limit; `encode`, `decode`, `zip-latest`, `throttle` and `share` did not exist;
# `unique(:expires)`, `squish(:with)` and `repeated` were ignored or empty;
# `rotor` emitted Lists where Rakudo emits Arrays; `batch` passed values through
# unbatched; `elems` answered a count instead of a supply of running counts; a
# one-parameter `&by` for min/max was called as a comparator and died; and
# `zip`'s tuples were not itemized.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $desc) {
    @fail.push("$desc: got {$got.raku}, want {$want.raku}") unless $got eqv $want;
}

# --- S-27  merge ------------------------------------------------------------
check Supply.merge().list, (), 'merging nothing is an empty supply';
check Supply.from-list(1, 2).merge(Supply.from-list(3)).list.sort.List, (1, 2, 3), 'merge takes every value';
check Supply.merge(Supply.from-list(1, 2)).list, (1, 2), 'merging one supply is that supply';
check ((try Supply.merge(Supply, Supply.from-list(1)).list) // $!.^name),
      'X::Supply::Combinator', 'an undefined Supply is a combinator error';

# --- S-28/S-29  reduce and produce -----------------------------------------
check Supply.from-list().reduce(&[+]).list, (Nil,), 'reduce of nothing is Nil';
check Supply.from-list(1, 2, 3).reduce(&[+]).list, (6,), 'reduce emits one value at the end';
check Supply.from-list(1, 2, 3).produce(&[+]).list, (1, 3, 6), 'produce emits every running value';
check Supply.from-list().produce(&[+]).list, (), 'produce of nothing emits nothing';

# --- S-30  migrate ----------------------------------------------------------
# (reified inside the `try`: Rakudo raises when the list is pulled from, which
# can be later than the call that asked for it)
check ((try { my @l = Supply.from-list(1, 2, 3).migrate.list; @l.elems }) // $!.^name),
      'X::Supply::Migrate::Needs', 'migrate wants Supplies';
check Supply.from-list(Supply.from-list(1, 2), Supply.from-list(3)).migrate.list, (1, 2, 3),
      'migrate emits the values of each inner supply';

# --- S-31  classify and categorize emit Pairs of supplies -------------------
check Supply.from-list(1, 2, 3, 4).classify(* % 2).list.map({ .key => .value.list }).List,
      ((1 => (1, 3)), (0 => (2, 4))), 'classify emits a key => Supply per key, first seen first';
check Supply.from-list(1, 2, 3).categorize({ $_ %% 2 ?? <even all> !! <odd all> })
            .list.map({ .key => .value.list }).List,
      ((odd => (1, 3)), (all => (1, 2, 3)), (even => (2,))), 'categorize sends a value to each key';
check Supply.from-list(1, "1", 1).classify({ $_ }).list.elems, 2, 'keys are compared by identity';

# --- S-32  comb, per chunk with a carry -------------------------------------
check Supply.from-list("ab", "c").comb.list, ("a", "b", "c"), 'comb defaults to characters';
check Supply.from-list("abc", "de").comb(2).list, ("ab", "cd", "e"),
      'an n-character comb crosses chunk boundaries and emits the leftover';
check Supply.from-list("abc").comb(0).list, ("a", "b", "c"), 'comb(0) is characters';
check Supply.from-list("xax", "ax").comb("a").list, ("a", "a"), 'a needle, once per occurrence';
check Supply.from-list("xa", "bx").comb("ab").list, ("ab",), 'a needle split across chunks is completed';
check Supply.from-list("abc").comb("").list, ("a", "b", "c"), 'an empty needle is characters';
check Supply.from-list("a1b22", "3c").comb(/\d+/).list, ("1", "22", "3"),
      'a regex match never grows across a chunk boundary';
check Supply.from-list("a1b2", "2c").comb(/\d+/, :match).list, ("1", "2", "2"), ':match combs the same way';
check Supply.from-list("a1b2c3").comb(/\d/, 2).list, ("1", "2"), 'a limit keeps the first n';

# --- S-33  split ------------------------------------------------------------
check Supply.from-list("a,b", ",c,").split(",").list, ("a", "b", "c", ""),
      'the carried piece is emitted at the end, even when empty';
check Supply.from-list("a,,b").split(",", :skip-empty).list, ("a", "b"), ':skip-empty drops empties';
check Supply.from-list("a,b,c").split(",", 2).list, ("a", "b"), 'a limit keeps the first n pieces';

# --- S-34  encode and decode ------------------------------------------------
check Supply.from-list("hi").encode.list[0].^name, 'utf8', 'encode makes a Blob of that encoding';
check Supply.from-list("hi").encode("latin-1").list[0].elems, 2, 'and honours the encoding named';
check Supply.from-list("ab".encode, "c".encode).decode.list, ("a", "b", "c"),
      'decode holds back the last character of each chunk';
check Supply.from-list("a".encode).decode.list, ("a",),
      'a one-character chunk emits nothing until the end';

# --- S-35/S-36/S-37  unique, squish, repeated -------------------------------
check Supply.from-list(1, 2, 1, 3, 2).unique.list, (1, 2, 3), 'unique keeps first sightings';
check Supply.from-list(1, "1", 1).unique.list, (1, "1"), 'identity, so 1 and "1" differ';
check Supply.from-list(<ab cd ef>).unique(:as(*.chars)).list, ("ab",), ':as maps to the key';
check Supply.from-list(1, 2, 3, 4).unique(:with(-> $a, $b { $a %% 2 == $b %% 2 })).list, (1, 2),
      ':with is the comparator';
{
    my $s = Supplier.new;
    my @o;
    $s.Supply.unique(:expires(0.05)).tap({ @o.push($_) });
    $s.emit(1); $s.emit(1); sleep 0.1; $s.emit(1);
    check @o, [1, 1], ':expires lets a key through again once the time has passed';
}
check Supply.from-list(1, 1, 2, 2, 1).squish.list, (1, 2, 1), 'squish drops consecutive duplicates';
check Supply.from-list(<a A b>).squish(:as(&lc)).list, ("a", "b"), 'squish :as';
check Supply.from-list(1, 2, 4, 5).squish(:with(-> $last, $v { $v == $last + 1 })).list, (1, 4),
      'squish :with sees the previously KEPT value first';
check Supply.from-list(1, 1, 2, 1, 3).repeated.list, (1, 1), 'repeated emits from the second sighting';
check Supply.from-list(<a A b>).repeated(:as(&lc)).list, ("A",), 'repeated :as';

# --- S-38  rotor emits Arrays ----------------------------------------------
check Supply.from-list(1..7).rotor(3 => -1).list, ([1, 2, 3], [3, 4, 5], [5, 6, 7]),
      'a negative gap overlaps, and each batch is an Array';
check Supply.from-list(1..7).rotor(2, :partial).list, ([1, 2], [3, 4], [5, 6], [7]),
      ':partial emits the short last batch';
check Supply.from-list(1..7).rotor(2).list, ([1, 2], [3, 4], [5, 6]), 'without :partial it is dropped';
check Supply.from-list(1..7).rotor(1, 2).list, ([1], [2, 3], [4], [5, 6], [7]), 'the cycle repeats';

# --- S-39  batch ------------------------------------------------------------
check Supply.from-list(1, 2, 3).batch.list, ((1,), (2,), (3,)), 'the default is one-element batches';
check Supply.from-list(1..5).batch(:2elems).list, ((1, 2), (3, 4), (5,)), ':elems groups';
check Supply.from-list(1..5).batch(:elems(-1)).list, ((1,), (2,), (3,), (4,), (5,)),
      'a non-positive :elems is the one-element default';

# --- S-41  elems is a supply of RUNNING counts ------------------------------
check Supply.from-list(<a b c>).elems.list, (1, 2, 3), 'elems emits the count after every value';
# …and the timed form reports the final count at done. Rakudo emits nothing
# here (the sheet's flagged bug); the count is what the caller asked for.
check Supply.from-list(<a b c>).elems(10).list, (3,), 'elems($seconds) still reports the total';

# --- S-42  head, tail, skip -------------------------------------------------
check Supply.from-list(1, 2, 3).head.list, (1,), 'head is the first value';
check Supply.from-list(1, 2, 3).head(0).list, (), 'head(0) is empty';
check Supply.from-list(1, 2, 3).head(*).list, (1, 2, 3), 'head(*) is the invocant';
check Supply.from-list(1, 2, 3).head(*-1).list, (1, 2), 'head(*-n) drops the last n';
check Supply.from-list(1, 2, 3).tail.list, (3,), 'tail is the last value';
check Supply.from-list().tail.list, (Any,), 'tail of an empty source is Any';
check Supply.from-list(1, 2, 3).tail(2).list, (2, 3), 'tail(n) is the last n';
check Supply.from-list(1, 2, 3).tail(*-1).list, (2, 3), 'tail(*-n) is skip(n)';
check Supply.from-list(1, 2, 3).skip.list, (2, 3), 'skip drops one';
check Supply.from-list(1, 2, 3).skip("1").list, (2, 3), 'skip coerces its argument';

# --- S-43  min, max, minmax -------------------------------------------------
check Supply.from-list(3, 1, 2).min.list, (3, 1), 'min emits every strict improvement';
check Supply.from-list(3, 1, 2).max.list, (3,), 'max the same';
check Supply.from-list(3, Any, 1).min.list, (3, 1), 'an undefined value is ignored';
check Supply.from-list(<bb a ccc>).min(*.chars).list, ("bb", "a"), 'a one-parameter &by is a key extractor';
check Supply.from-list(<bb a ccc>).max(-> $a, $b { $a.chars <=> $b.chars }).list, ("bb", "ccc"),
      'a two-parameter &by is the comparator';
check Supply.from-list(3, 1, 2).minmax.list, (3..3, 1..3), 'minmax emits the running Range';
check Supply.from-list(<bb a ccc>).minmax(*.chars).list.map(*.raku).List,
      ('"bb".."bb"', '"a".."bb"', '"a".."ccc"'), 'a Str minmax keeps its endpoints';

# --- S-44  grab, reverse, sort, collate, rotate -----------------------------
check Supply.from-list(1, 2, 3).grab(*.reverse).list, (3, 2, 1), 'grab hands over the whole stream';
check Supply.from-list(3, 1, 2).sort.list, (1, 2, 3), 'sort is a grab';
check Supply.from-list(<b a>).reverse.list, ("a", "b"), 'reverse is a grab';
check Supply.from-list(<b A a>).collate.list, ("a", "A", "b"), 'collate is a grab';
check Supply.from-list(1, 2, 3, 4).rotate.list, (2, 3, 4, 1), 'rotate holds the first n back';
check Supply.from-list(1, 2).rotate(3).list, (2, 1), 'rotate past the end wraps';
check Supply.from-list(1, 2, 3).rotate(0).list, (1, 2, 3), 'rotate(0) is the invocant';

# --- S-45/S-46  zip and zip-latest -----------------------------------------
check Supply.zip(Supply.from-list(1, 2, 3), Supply.from-list(4, 5)).list.map(*.raku).List,
      ('$(1, 4)', '$(2, 5)'), 'zip emits an itemized row per source';
check Supply.zip(Supply.from-list(1, 2), Supply.from-list(3, 4), :with(&[+])).list, (4, 6), 'zip :with folds';
check Supply.zip().list, (), 'zipping nothing is empty';
check ((try Supply.zip(Supply).list) // $!.^name), 'X::Supply::Combinator', 'zip checks its arguments';
check Supply.from-list(1, 2).zip.list, (1, 2), 'zipping one supply is that supply';
check Supply.from-list(1, 2, 3).zip-latest(Supply.from-list(4, 5)).list.map(*.raku).List,
      ('$(3, 4)', '$(3, 5)'), 'zip-latest waits for every source, then tracks the latest';
check Supply.zip-latest(Supply.from-list(1, 2), Supply.from-list(3), :initial(0, 0)).list.map(*.raku).List,
      ('$(1, 0)', '$(2, 0)', '$(2, 3)'), ':initial seeds the sources in order';
check Supply.zip-latest(Supply.from-list(1), Supply.from-list(2), :with(&[+])).list, (3,), 'zip-latest :with';

# --- S-47  throttle ---------------------------------------------------------
{
    my $s = Supplier.new;
    my @o;
    $s.Supply.throttle(2, 0.05).tap({ @o.push($_) });
    $s.emit($_) for 1 .. 5;
    my @first = @o.clone;
    sleep 0.3;
    # how many ticks elapse while five emits run is wall-clock, so the claim is
    # the one that does not depend on it: the stream is PACED, not passed through
    check (@first.elems < 5), True, 'not every value passes at once';
    check @o, [1, 2, 3, 4, 5], 'the rest follow on later ticks, in order';
}
check Supply.from-list(1, 2, 3).throttle(2, { $_ * 10 }).list.map(*.^name).unique.List, ('Promise',),
      'the concurrency form emits Promises';
check Supply.from-list(1, 2, 3).throttle(2, { $_ * 10 }).list.map(*.result).sort.List, (10, 20, 30),
      'and each of them holds its result';

# --- S-48  share is HOT: what happened before a tap existed is lost ---------
{
    my $s = Supply.from-list(1, 2, 3).share;
    my @o;
    $s.tap({ @o.push($_) });
    check @o, [], 'a finished source has nothing left to share';
}
{
    my $sup = Supplier.new;
    my $sh = $sup.Supply.share;
    my @o;
    $sh.tap({ @o.push("a$_") });
    $sh.tap({ @o.push("b$_") });
    $sup.emit(1);
    check @o, ['a1', 'b1'], 'one subscription, handed to every tap';
}

if @fail {
    note "FAILED:\n" ~ @fail.map({ "  - $_" }).join("\n");
    exit 1;
}
say "PASS";
