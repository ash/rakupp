# Regression: what a Hash keys on, what a Capture answers, and the Cool subs that
# were selecting their source by VALUE TAG instead of by position.
#   * `.keyof` never looked at the declared key type. Parser records `my %h{Int}`
#     as declType "Any,Int" and `.of` already reads the value half; `.keyof` now
#     reads the key half. A plain hash keys on the COERCION type Str(Any).
#   * `\(…) ~~ :(…)` asks whether the capture would BIND, which is
#     Signature.ACCEPTS's job — the generic smartmatch had no idea what a
#     Signature was and answered False for every capture.
#   * `.kv` on a Capture is index/value for the positionals then name/value for
#     the nameds; falling through to Array.kv yielded the named part as
#     (index, Pair).
#   * `words`/`lines` are Cool routines, so `words(42)` is "42".words. Selecting
#     the source by `v.t == VT::Str` sent every other type to the `$*IN` branch,
#     where it BLOCKED ON STDIN.
#   * `printf($fmt, $junction)` prints once per eigenstate, in order. sprintf
#     does NOT autothread — the junction stays one value there.
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) { @fail.push("$what: got $got want $want") unless $got eq $want }

# keyof
my %plain = 'apples' => 3;
check(%plain.keyof.gist, '(Str(Any))', 'a plain hash keys on the coercion type');
check(Hash.keyof.gist,   '(Str(Any))', 'and so does the type object');
my %oi{Int}; %oi{42} = 1;
check(%oi.keyof.gist, '(Int)', 'an object hash keys on its declared type');
my %oa{Any};
check(%oa.keyof.gist, '(Any)', 'whatever that type is');
my Int %tv{Str};
check(%tv.keyof.gist, '(Str)', 'the key half is read, not the value half');
check(%tv.of.gist,    '(Int)', 'and .of still reads the value half');
check(Mix.keyof.gist, '(Mu)',  'an unparameterised quanthash is unaffected');

# a Capture against a Signature
check((so \(7)        ~~ :($)).gist,    'True',  'one positional binds one');
check((so \(7, 8)     ~~ :($)).gist,    'False', 'two do not');
check((so \(a => 1)   ~~ :(:$a)).gist,  'True',  'a named binds a named');
check((so \(1, :a(2)) ~~ :($, :$a)).gist, 'True', 'and both together');
# …and Signature ~~ Signature is a DIFFERENT question: is every call that binds
# the left also bound by the right? A slurpy named does not widen the positional
# window, so `:(*%) ~~ :()` needs more than the arity/count comparison.
check((:($)    ~~ :($)).gist,   'True',  'the same signature contains itself');
check((:($)    ~~ :()).gist,    'False', 'a one-arg call does not bind :()');
check((:()     ~~ :($)).gist,   'False', 'nor a no-arg call bind :($)');
check((:($,$)  ~~ :($)).gist,   'False', 'two positionals into one');
check((:(*@)   ~~ :(*@)).gist,  'True',  'slurpy into slurpy');
check((:(*@)   ~~ :()).gist,    'False', 'slurpy into nothing');
check((:(*%)   ~~ :()).gist,    'False', 'a slurpy NAMED into nothing');
check((:()     ~~ :(*%)).gist,  'True',  'but nothing into a slurpy named');

# Capture.kv
my $c = \(2, 3, apples => (red => 2));
check($c.kv.gist,     '(0 2 1 3 apples red => 2)', 'kv is index/value then name/value');
check($c.keys.gist,   '(0 1 apples)',              'keys');
check($c.values.gist, '(2 3 red => 2)',            'values');
check($c.elems,       '2',                         'elems counts positionals');

# the Cool subs take the first positional whatever its type
check(words(42).gist,       '(42)',    'words of an Int');
check(lines(42).gist,       '(42)',    'lines of an Int');
check(words('a b c').gist,  '(a b c)', 'words of a Str is unchanged');
check(words('a b c', 2).gist, '(a b)', 'and still takes a limit');
check(lines("a\nb").gist,   '(a b)',   'lines of a Str');

if @fail { note "FAILED: @fail.join('; ')"; say 'FAIL' } else { say 'PASS' }
