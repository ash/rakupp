# Regression: when a Str range is EMPTY, where it STOPS, and the three places it
# used to run away to the 1,000,000-element cap.
#   * the emptiness guard is a plain whole-string compare of the RAW endpoints,
#     `min gt max`. It was length-first, so ("Y".."AB") yielded Y Z AA AB — and
#     ('fig'..'banana'), 3 chars climbing toward a 6-char endpoint it can never
#     reach, ran to the cap and peaked at 952 MB of resident memory.
#   * the SEED is emitted whatever its length; it is each SUCCESSOR that is tested,
#     against BOTH the endpoint and the endpoint's length. Testing the current
#     value instead made ('aa'..'b') empty and ('a'..'aa') the whole alphabet.
#   * lengths count CODEPOINTS, not bytes — on the byte count every multi-byte
#     character looked longer than an ASCII endpoint.
#   * the `...` sequence operator had no single-codepoint path, so ('☀' ... '☕')
#     re-pushed the unchanged seed a million times (11 MB of ☀) where `..` already
#     walked codepoints correctly.
#   * `for 'a'..'c'` under --exe compiled to a `long long` counter over
#     `.toInt()` of the endpoints — 0..0 — so the native binary printed one `0`
#     while the interpreter printed a b c. (The native check lives in t/run.raku;
#     what is pinned here is the interpreted behaviour it must match.)
# NOT fixed here, and deliberately so: Rakudo iterates an EQUAL-LENGTH multi-char
# range as a per-position cross product, so ('ab'..'ba') is ("ab","aa","bb","ba")
# where we still give the 26-element succ chain. Every assertion below is one that
# holds under both orderings.
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) { @fail.push("$what: got $got want $want") unless $got eq $want }

# the emptiness guard, on the raw endpoints
check(~("Y".."AB"),           '',  'a shorter min that sorts after max is empty');
check(("fig".."banana").elems, 0,  'and does not climb toward an unreachable endpoint');
check(("y".."ab").elems,       0,  'likewise across lengths');
check(("b".."aa").elems,       0,  'and one char against two');
check(("ba".."ab").elems,      0,  'equal lengths, min after max');
check(("b".."a").elems,        0,  'the single-char case still empties');

# the successor is what gets tested, not the current value
check(("aa".."b").list.gist,  '(aa)', 'the seed is emitted whatever its length');
check(("az".."b").list.gist,  '(az)', 'and its successor is too long');
check(("a".."aa").list.gist,  '(a)',  "'b' already sorts after 'aa'");
check(("a".."ba").list.gist,  '(a b)',   'stopping when the successor passes max');
check(("a".."cc").list.gist,  '(a b c)', 'three of them');
check(("a".."zz").elems,      702,  'the full two-letter climb');
check(("a".."zzz").elems,   18278,  'and the three-letter one');
check(("zz".."zzz").elems,  17577,  'starting part-way in');

# exclusions
check(("a"..^"zz").elems,  701, 'excluding max drops it only when it is reached');
check(("a"^.."zz").elems,  701, 'excluding min seeds from min.succ');
check(("a"^..^"zz").elems, 700, 'both');
check(("az"^.."ba").list.gist, '(ba)', 'the guard runs on the RAW endpoints, before ^ seeds');
check(("cc".."cc").list.gist,  '(cc)', 'min eq max is one element');
check(("cc"..^"cc").list.gist, '()',   'and none when max is excluded');

# codepoints, not bytes
check(('α'..'ω').elems,   25, 'a single-codepoint Greek range');
check(('☀'..'☕').elems,  22, 'and a symbol range');
check(('a'..'c').WHAT.gist, '(Range)', 'single-codepoint endpoints are a real Range');

# the `...` sequence operator walks codepoints when both ends are one codepoint
check(('☀' ... '☕').elems, 22, 'the sequence operator agrees with the range');
check(('a' ... 'e').gist, '(a b c d e)', 'ordinary ascending is unchanged');
check(('e' ... 'a').gist, '(e d c b a)', 'and descending');
check(('a' ...^ 'e').gist, '(a b c d)',  'and the exclusive form');
check(('a' ... 'z').elems, 26, 'a full alphabet');
check(('Z' ... 'a').elems,  8, 'crossing the punctuation between the cases');

# iterating one still works everywhere it did
my @c; for 'a'..'c' { @c.push($_) }
check(@c.join, 'abc', 'for over a Str range');
check(<a b c>.grep(* ~~ 'a'..'b').join, 'ab', 'membership in a Str range');
check(('a'..'e').list.reverse.join, 'edcba', 'reversing one');

if @fail { note "FAILED: @fail.join('; ')"; say 'FAIL' } else { say 'PASS' }
