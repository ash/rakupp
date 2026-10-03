# A WebSocket chat room with Cro: a server and three clients in one program.
#
# The server speaks JSON (`web-socket :json`). Every message a client sends is
# stamped with a sequence number and broadcast to everyone in the room, and the
# room announces who joins and who leaves (the handler's second argument is a
# Promise kept when the client goes). Each client listens in a `react` block
# and reads every message body with a nested `whenever` — an `await` there
# would block the very stream that delivers the body. An echo endpoint shows
# text and binary frames on their own.
#
#     rakupp websocket-chat.raku
#
# The port is 20304, or CRO_EXAMPLE_PORT when that is set.

use Cro::HTTP::Router;
use Cro::HTTP::Router::WebSocket;
use Cro::HTTP::Server;
use Cro::WebSocket::Client;

my $room = Supplier.new;
my $seq = 0;

my $application = route {
    get -> 'room', $name {
        web-socket :json, -> $incoming, $closed {
            supply {
                whenever $room -> $event { emit $event }
                whenever $incoming -> $message {
                    my $json = await $message.body;
                    $room.emit({ seq => ++$seq, from => $name, text => $json<text> });
                }
                whenever $closed {
                    $room.emit({ seq => ++$seq, from => 'room', text => "$name left" });
                }
                $room.emit({ seq => ++$seq, from => 'room', text => "$name joined" });
            }
        }
    }
    get -> 'echo' {
        web-socket -> $incoming {
            supply whenever $incoming -> $m {
                emit $m.is-text ?? (await $m.body-text).flip !! (await $m.body-blob).reverse;
            }
        }
    }
}

my $port = %*ENV<CRO_EXAMPLE_PORT> // 20304;
my $server = Cro::HTTP::Server.new(:host<127.0.0.1>, :$port, :$application);
$server.start;
LEAVE $server.stop;

# --- echo: a text frame comes back reversed, a binary one too
my $echo = await Cro::WebSocket::Client.connect("ws://127.0.0.1:$port/echo");
my $replies = Channel.new;
$echo.messages.tap: -> $m {
    $m.is-text ?? $m.body-text.then({ $replies.send("text {.result}") })
               !! $m.body-blob.then({ $replies.send("binary {.result.list}") });
}
$echo.send('stressed');
say 'echo: ', $replies.receive;
$echo.send(Blob.new(1, 2, 3));
say 'echo: ', $replies.receive;
$echo.close;

# --- the room: each client keeps a log of what it saw
my %log;
my %conn;
my @listeners;
sub join-room($who) {
    my $conn = await Cro::WebSocket::Client.new(:json).connect("ws://127.0.0.1:$port/room/$who");
    %log{$who} = [];
    %conn{$who} = $conn;
    @listeners.push: start react {
        whenever $conn.messages -> $m {
            whenever $m.body -> $e { %log{$who}.push("{$e<seq>} {$e<from>}: {$e<text>}") }
        }
    }
    sleep 0.3;   # let the join announcement go round before the next step
}
sub say-in-room($who, $text) { %conn{$who}.send({ :$text }); sleep 0.3 }

join-room 'ann';
join-room 'bob';
say-in-room 'ann', 'hi all';
join-room 'cat';
say-in-room 'bob', 'hello cat';
%conn<cat>.close;
sleep 0.3;
say-in-room 'ann', 'bye';

for <ann bob cat> -> $who {
    say "$who saw:";
    say "  $_" for %log{$who}.list;
}

# when a connection closes, its client's react block ends
.close for %conn<ann bob>;
await Promise.anyof(Promise.allof(@listeners), Promise.in(5));
say 'listeners: ', @listeners».status.join(' ');
