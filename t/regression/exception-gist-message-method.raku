# Regression: an Exception whose `message` is a METHOD must gist to it.
#
# The gist path looked for a `message` ATTRIBUTE. The base Exception hands one
# down undefined, so a class that COMPUTES its message — which is most of them
# in the wild; X::Protocol builds "HTTP error: 404" out of two other fields —
# gisted to the empty string. `~$e` was right the whole time, because
# prefixStringify asks the method, so `say $e` printed a BLANK LINE while
# `say ~$e` printed the message.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) {
    @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want
}

class X::Proto is Exception {
    has $.status;
    has $.protocol;
    method message() { "$.protocol error: $.status" }
}
my $e = X::Proto.new(status => 404, protocol => 'HTTP');

check $e.message, 'HTTP error: 404', 'the method answers';
check $e.gist,    'HTTP error: 404', 'and the gist is the message';
check $e.Str,     'HTTP error: 404', 'as Str always was';
check ~$e,        'HTTP error: 404', 'and ~';

# `say` goes through gist: the blank line is the symptom a user sees.
my $out = $*TMPDIR.add("rakupp-exc-gist-{$*PID}.txt");
{ my $fh = $out.open(:w); my $*OUT = $fh; say $e; $fh.close }
check $out.slurp.chomp, 'HTTP error: 404', 'say prints the message';
$out.unlink;

# An attribute that IS set still wins — built-in X:: classes carry one.
class X::Attr is Exception { has $.message }
check X::Attr.new(message => 'from the attribute').gist, 'from the attribute',
      'a set message attribute is used as before';

# X::AdHoc's message is its payload, and stays so.
check X::AdHoc.new(payload => 'boom').gist, 'boom', 'AdHoc gists to its payload';

# A method that returns the empty string is not a reason to fall back.
class X::Empty is Exception { method message() { '' } }
check X::Empty.new.message, '', 'an empty message is still a message';

# The same shape under a CATCH.
class X::Thrown is Exception {
    has $.code;
    method message() { "failed with $.code" }
}
my $caught = '';
{ X::Thrown.new(code => 7).throw; CATCH { default { $caught = .gist.lines[0] } } }
check $caught, 'failed with 7', 'a thrown one gists to its message too';

if @fail {
    .say for @fail;
    say "FAIL";
    exit 1;
}
say "PASS";
