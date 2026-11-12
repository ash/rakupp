# Regression (#259): a hash STORE of Nil is the odd-number error, as Rakudo
# has it and the docs require. Hash assignment iterates its right side, which
# must hold an even number of elements, and in a list Nil takes one slot
# (Empty is the value that takes none); the Nil page names `%h = Nil` as the
# error outright. The undefined Any a variable holds after `= Nil` is one item
# too, which Rakudo reports as "type object 'Any'".
#
# Both were an empty Hash here, while `%h = 1` and `%h = (Nil,)` threw.
# `%(…)` and `%$x` are `.hash`, a coercion: an undefined value there is still
# the empty Hash (`%(Any)` had thrown).
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) {
    @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want
}
sub odd(&code, $what, $saw) {
    code();
    @fail.push("$what: no exception");
    CATCH {
        default {
            @fail.push("$what: {.^name}") unless $_ ~~ X::Hash::Store::OddNumber;
            check .message.lines.tail, "Only saw: $saw", "$what: the message";
            check .found, 1, "$what: .found";
        }
    }
}

odd { my %h = Nil }, 'my %h = Nil', 'Nil';
odd { my %h; %h = Nil }, '%h = Nil', 'Nil';
odd { my $v = Nil; my %h = $v }, 'a variable reset by Nil', "type object 'Any'";
odd { my %h = Int }, 'a type object', "type object 'Int'";
odd { my %h is default(5) = Nil }, 'is default', 'Nil';
odd { my %h{Int} = Nil }, 'an object hash', 'Nil';
odd { my %h; %h.STORE(Nil) }, '.STORE(Nil)', 'Nil';
odd { sub f { return }; my %h = f() }, 'a bare return', 'Nil';

my %e = Empty;  check %e, {}, 'Empty takes no slot';
my %l = ();     check %l, {}, 'an empty list';
check %(Nil), {}, '%(Nil) is a coercion';
check %(Any), {}, '%(Any)';
my $x;
check %$x, {}, '%$x of an unset $x';
my @a = Nil;    check @a.elems, 1, 'an array takes the one Nil';

if @fail {
    note "FAILED:\n" ~ @fail.map({ "  - $_" }).join("\n");
    exit 1;
}
say "PASS";
