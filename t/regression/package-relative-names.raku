# Regression: the package-relative-names batch (URI + LibraryMake, Cro loading).
#  1. A qualified class answers to its TAIL when free (use URI::Path; bare Path)
#     — but never shadows a BUILT-IN type (X::Roast::Channel must not hijack Channel).
#  2. A class nested in a class body registers QUALIFIED (Outer::Inner), with the
#     short name still resolving — including `.= new` on a typed my-decl.
#  3. `use X` inside a class body loads the module at declaration.
#  4. .can() reports built-in methods (new/gist/...; parse/subparse on grammars).
#  5. Char-class composition with USER tokens: <[..] +tok>, <+tok - [..]>,
#     kebab-case token names in class composition.
#  6. Bracketed infix `[op]` / metaop assignment `[R//]=` (left target).
my $ok = 0; my $n = 0;
sub ck($got, $want, $desc) {
    $n++;
    if $got eqv $want { $ok++ }
    else { say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

# 2. nested class naming + short-name resolution
class Forest {
    class Frog { method speak() { "ribbit" } }
    has Frog $.frog;
    method new() {
        my Frog $frog .= new;
        self.bless(:$frog);
    }
}
ck(Forest.new.frog.speak, 'ribbit', 'typed .= new on a nested class');
ck(Forest::Frog.new.speak, 'ribbit', 'nested class answers its qualified name');
ck(Forest::Frog.^name, 'Forest::Frog', 'nested ^name is qualified');

# 1. built-in tails are protected
class X::Test::Channel { method boom() { "boom" } }
ck(Channel.new.^name.contains('Channel'), True, 'bare Channel stays the built-in');
ck(X::Test::Channel.new.boom, 'boom', 'the qualified exception class still works');

# 4. .can on built-ins + grammars
grammar GTest { token TOP { \w+ } }
ck(GTest.can('parse').Bool, True, 'grammar .can(parse)');
ck(Forest.can('new').Bool, True, 'class .can(new)');
ck(Forest.can('no-such-method').Bool, False, '.can stays honest for unknowns');

# 5. char-class composition with user tokens (RFC 3986 style)
grammar CC {
    token uri-alpha { <alpha> }
    token scheme { <.uri-alpha> <[\-+.] +uri-alpha +digit>* }
    token seg { [ <+uri-alpha - [x]> ]+ }
}
ck(CC.subparse('http9+x', :rule<scheme>).Str, 'http9+x', 'composed class: bracket + user tokens');
ck(CC.subparse('abxy', :rule<seg>).Str, 'ab', 'composed class: user token minus bracket');

# 6. bracketed infix + metaop assignment
my %v = a => 1;
my %e = a => 99, b => 7;
%v<a> [R//]= %e<a>;
%v<c> [R//]= %e<b>;
ck(%v<a>, 99, '[R//]= takes the defined right value');
ck(%v<c>, 7, '[R//]= fills an undefined left slot');
ck((1 [+] 2), 3, 'bracketed [+] is plain +');
ck((5 R// 3), 3, 'R// reverses defined-or');

say $ok == $n ?? 'PASS' !! "FAIL ($ok/$n)";
