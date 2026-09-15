# `::?CLASS` inside a ROLE BODY — not inside one of its methods, where `self`
# answers — resolved to Mu, so `my &AT-KEY := ::?CLASS.^find_method('AT-KEY')`
# found nothing and the call it guards answered the container itself. And the
# lookup has to give the ORIGINAL: Rakudo runs a role body per composition,
# before the role's own methods reach the class, so a role overriding AT-KEY and
# calling the captured one from inside it gets the built-in. Ours ran once with
# the override already registered, so that call recursed 32,000 frames deep.
# WriteOnceHash and AccountableBagHash are both written this way.
use Test;
plan 4;

role Peek {
    my &orig := ::?CLASS.^find_method('AT-KEY');
    method original-name { &orig.name }
    method AT-KEY(::?CLASS:D: $key is raw) is raw { orig(self, $key) }
}
class PeekHash is Hash does Peek { }

my %h is PeekHash = a => 42, b => 7;
is %h.original-name, 'AT-KEY',  'the role body finds the original method';
is %h<a>, 42,                   'an override may call it without recursing';
is %h<b>, 7,                    '…for every key';

role Named { my $n = ::?CLASS.^name; method body-class { $n } }
class NamedUser does Named { }
isnt NamedUser.body-class, 'Mu', 'a role body knows a class name at all';
