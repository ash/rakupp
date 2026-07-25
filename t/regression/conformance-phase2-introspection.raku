# Regression: raku-spec conformance Phase 2 — the introspection and coercion
# routines whose absence made documented examples die on dispatch.
#   1. Parameter's full API. Three defects sat behind it: an anonymous
#      parameter's .name was its bare sigil; `is copy` was never recorded in the
#      main signature-trait loop and `is raw` not at all; and .type for an
#      unconstrained parameter is Any on a ROUTINE but Mu on a `:( … )` literal.
#   2. `.dynamic` asks about the VARIABLE — only the `*` twigil in the name
#      makes a container dynamic.
#   3. `is default(v)` answers on a missing HASH key, not just an Array index;
#      a QuantHash has a typed default (False / 0), not Any.
#   4. a Failure's .handled is assignable, and a bare `fail` still carries an
#      X::AdHoc so .exception.message answers.
#   5. `.self` is the invocant.
#   6. IO::Path.parts / IO::Path::Parts, and Str -> Date/DateTime/Version.
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) { @fail.push("$what: got $got want $want") unless $got eq $want }

# 1. Parameter introspection
my $sig = :(Str $x, Bool, $r is raw, $c is copy, $o = 5, Int :$n!, *@rest);
my @p = $sig.params;
check(@p[0].name,        '$x',   'param-name');
check(@p[0].usage-name,  'x',    'param-usage-name');
check(@p[1].name,        '',     'anonymous-param-has-no-name');
check(@p[0].type.gist,   '(Str)', 'param-type');
check(@p[2].raw,         'True',  'is-raw-tracked');
check(@p[3].copy,        'True',  'is-copy-tracked');
check(@p[0].readonly,    'True',  'plain-param-readonly');
check(@p[2].readonly,    'False', 'raw-param-not-readonly');
check(@p[4].default.defined, 'True',  'param-with-default');
check(@p[0].default.defined, 'False', 'param-without-default');
check(@p[5].named,       'True',  'named-param');
check(@p[5].positional,  'False', 'named-is-not-positional');
check(@p[6].slurpy,      'True',  'slurpy-param');
check(@p[6].type.gist,   '(Positional)', 'slurpy-sigil-implies-Positional');
check(:(*@a).params[0].prefix,  '*',  'param-prefix');
check(:($x!).params[0].suffix,  '',   'param-suffix-positional');
check(:(Int:D $x).params[0].modifier, ':D', 'param-modifier');
# an unconstrained param: Any on a routine, Mu on a bare signature literal
sub plain($u) { }
check(&plain.signature.params[0].type.gist, '(Any)', 'routine-untyped-is-Any');
check(:($u).params[0].type.gist,            '(Mu)',  'literal-untyped-is-Mu');

# 2. .dynamic
my @a; my @*d;
check(@a.dynamic, 'False', 'plain-not-dynamic');
check(@*d.dynamic, 'True', 'twigil-is-dynamic');

# 3. container defaults
my @b is default(42);
check(@b.default, '42', 'array-default');
check(@b[7],      '42', 'array-default-applies');
my %g is default(9);
check(%g.default,   '9', 'hash-default');
check(%g<missing>,  '9', 'hash-default-applies-on-missing-key');
check(set(<a b>).default.gist, 'False', 'set-default-is-False');
check(bag(<a b>).default.gist, '0',     'bag-default-is-zero');
check(@a.default.gist,         '(Any)', 'plain-default-is-Any');

# 4. Failure
sub f() { fail }
my $v = f;
check($v.handled, 'False', 'failure-unhandled');
$v.handled = True;
check($v.handled, 'True',  'handled-is-assignable');
my $w = f;
check($w.exception.^name, 'X::AdHoc', 'bare-fail-carries-an-exception');
@fail.push('bare-fail-message') unless $w.exception.message.chars;

# 5. .self
check('42'.Int.self, '42', 'self-on-int');
check('ab'.self,     'ab', 'self-on-str');

# 6. IO::Path::Parts and the Str coercions
check("/tmp/a/b.txt".IO.parts.gist, 'IO::Path::Parts.new("","/tmp/a","b.txt")', 'io-parts');
my $pp = IO::Path::Parts.new("/", "home/ash", "f.txt");
check("{$pp.volume}|{$pp.dirname}|{$pp.basename}", '/|home/ash|f.txt', 'parts-accessors');
check("2024-06-01".Date.Str,   '2024-06-01', 'str-to-date');
check("1.2.3".Version.gist,    'v1.2.3',     'str-to-version');

if @fail { note "FAILED: @fail.join('; ')"; say 'FAIL' } else { say 'PASS' }
