# Regression: a metaclass `compose` saw a public attribute's accessor in
# `.^method_table` before its `callsame` had reached the base compose. Rakudo
# makes the accessor there (and only when no method of that name exists), so
# before it `$.bar` is no `bar` method — and `rakupp install AttrX::Lazy` failed
# its own test with "A method 'bar' already exists": the module tells a user's
# `method bar` from a lazy `$.bar` exactly that way, then installs its own
# `bar` with `^add_method` for the base compose to keep.
# Contract: exit 0 + last line PASS.
my @fail;
sub ck($got, $want, $desc) { @fail.push("$desc — got {$got.raku}, want {$want.raku}") unless $got eqv $want }

my %seen;
my role Hook {
    method compose(Mu \type) {
        my $n = type.^name;
        %seen{"$n before"} = type.^method_table.keys.sort.join(" ");
        %seen{"$n names"}  = type.^method_names.sort.join(" ");
        %seen{"$n local"}  = type.^methods(:local).map(*.name).sort.join(" ");
        type.^add_method('bar', method () { "hooked" }) unless 'bar' ~~ type.^method_table;
        callsame;
        %seen{"$n after"} = type.^method_table.keys.sort.join(" ");
    }
}
multi trait_mod:<is>(Attribute:D $attr, :$hooked!) {
    $attr.package.HOW does Hook unless $attr.package.HOW ~~ Hook;
}

class Plain {
    has $.bar is hooked;
    has $.x;
    has $!private;
    method m() { }
}
ck %seen{"Plain before"}, "m",   'before callsame: the declared methods only';
ck %seen{"Plain names"}, "m",   '.^method_names agrees';
ck %seen{"Plain local"}, "m",   '.^methods(:local) agrees';
ck %seen{"Plain after"}, "bar m x", 'after callsame: the accessors too';
ck Plain.new(bar => 5).bar, 'hooked', 'the added method stands in for the accessor';
ck Plain.new(x => 7).x, 7, 'an untouched accessor still answers';

class Declared {
    has $.bar is hooked;
    method bar() { "declared" }
}
ck %seen{"Declared before"}, "bar", "a declared `method bar` is listed before callsame";
ck Declared.new.bar, 'declared', 'and is the one that answers';

# outside a metaclass compose nothing changes
ck Plain.^method_table.keys.sort.join(" "), "bar m x", 'a composed class lists its accessors';

if @fail { note "FAILED:\n" ~ @fail.join("\n"); say 'FAIL' }
else     { say 'PASS' }
