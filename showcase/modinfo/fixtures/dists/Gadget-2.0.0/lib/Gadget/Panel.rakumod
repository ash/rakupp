unit class Gadget::Panel;

has Str $.title = 'untitled';

method render(--> Str) { "[ $!title ]" }
