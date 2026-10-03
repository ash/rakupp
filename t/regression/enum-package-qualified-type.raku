# Regression: an enum declared in a package is a type by its QUALIFIED name.
# `has Cro::WebSocket::Message::Opcode $.opcode` names the enum that
# `package Cro::WebSocket::Message { enum Opcode … }` declares; its values
# carry only the short name `Opcode`, and the attribute refused them
# ("expected Cro::WebSocket::Message::Opcode but got Opcode (Opcode::Text)").
# Contract: exit 0 + last line PASS.
my @fail;

package Q::Msg { enum Opcode (:Text(1), :Binary(2), :Close(8)); }
class Q::M { has Q::Msg::Opcode $.op is rw; }

my $m = Q::M.new(op => Q::Msg::Opcode::Text);
@fail.push("attr new") unless $m.op ~~ Q::Msg::Opcode::Text;
$m.op = Q::Msg::Opcode(8);
@fail.push("attr assign") unless $m.op ~~ Q::Msg::Opcode::Close;
my Q::Msg::Opcode $v = Q::Msg::Opcode::Binary;
@fail.push("my var") unless $v == 2;
sub takes(Q::Msg::Opcode $o) { $o.key }
@fail.push("param") unless takes(Q::Msg::Opcode::Close) eq 'Close';
@fail.push("smartmatch") unless Q::Msg::Opcode::Text ~~ Q::Msg::Opcode;

# a value of some other type is still refused
my $died = False;
try { Q::M.new(op => 42); CATCH { default { $died = True } } }
@fail.push("Int accepted") unless $died;

if @fail { note "FAILED: @fail[]"; say 'FAIL' } else { say 'PASS' }
