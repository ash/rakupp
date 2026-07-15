# chat — a concurrent TCP chat server

A multi-client chat server written directly on `IO::Socket::INET`, no framework.
Every connection is handled on its own `start` thread; a shared roster of clients
is guarded by a `Lock`, and each message is broadcast to everyone else. Where the
[pastebin](../pastebin/) is one request then one response, this keeps many
long-lived connections interleaving at once.

## Run it

```sh
build/rakupp showcase/chat/chat.raku            # listens on 127.0.0.1:6667
# or compile a standalone binary:
build/rakupp --exe -o chat showcase/chat/chat.raku && ./chat
PORT=7000 build/rakupp showcase/chat/chat.raku  # pick a port
```

Then connect with any line-oriented TCP client — one per terminal, one per person:

```sh
nc 127.0.0.1 6667
```

The **first line you send is your nick**; everything after it is broadcast to the
room. `/who` lists who's online, `/quit` disconnects.

## A live session

Four terminals: the server, and three people (`alice`, `bob`, `carol`) coming and
going. In each client terminal the short lines are what that person typed (their
nick, a message, `/who`, `/quit`); every other line arrived from the server.

**Terminal 1 — the server**

```
$ build/rakupp showcase/chat/chat.raku
rakupp-chat listening on 127.0.0.1:6667  (Ctrl-C to stop)
  + alice (id 0), 1 online
  + bob (id 1), 2 online
  + carol (id 2), 3 online
  - bob (id 1), 2 online
  - carol (id 2), 1 online
  - alice (id 0), 0 online
```

**Terminal 2 — alice** (connects first, stays for the whole session)

```
$ nc 127.0.0.1 6667
Welcome to rakupp-chat! Pick a nick: alice
Hi alice — you're online. /who to list, /quit to leave.
* bob joined
hey bob
bob: hi alice!
* carol joined
carol: hello everyone
/who
* online: alice, bob, carol
* bob left
* carol left
/quit
```

**Terminal 3 — bob** (joins second, leaves early)

```
$ nc 127.0.0.1 6667
Welcome to rakupp-chat! Pick a nick: bob
Hi bob — you're online. /who to list, /quit to leave.
alice: hey bob
hi alice!
* carol joined
carol: hello everyone
/quit
```

**Terminal 4 — carol** (joins last, leaves before alice)

```
$ nc 127.0.0.1 6667
Welcome to rakupp-chat! Pick a nick: carol
Hi carol — you're online. /who to list, /quit to leave.
hello everyone
* bob left
/quit
```

Notice a sender never receives their own message (broadcast skips the author), so
`bob`'s terminal shows the `hi alice!` he typed but not a `bob: hi alice!` echo —
and `alice`, still connected at the end, sees both `bob` and `carol` leave.

## Commands

| Line you send | Effect |
|---|---|
| *(first line)* | sets your nick |
| `…anything…` | broadcast to everyone else as `nick: …` |
| `/who` | server replies with the current roster |
| `/quit` | disconnects you (announced to the room) |

## How it works

- **One thread per client.** The accept loop does `start serve($conn)` and goes
  straight back to `accept`, so a slow or idle client never blocks the others.
  A blocking `recv` releases the interpreter lock, letting other clients flow.
- **Shared state under a `Lock`.** The client roster lives in `@clients`, guarded
  by `$lock.protect { … }` for every add / remove / snapshot.
- **Broadcast writes outside the lock.** `broadcast` snapshots the target sockets
  under the lock, then writes to them outside it, so one stalled client can't hold
  up the rest.
- **Survives abrupt disconnects.** Every socket write is guarded, and the runtime
  ignores `SIGPIPE`, so a client that vanishes mid-write (a dropped connection, a
  port scan) drops just itself — the server keeps running.
