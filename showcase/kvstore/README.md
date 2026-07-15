# kvstore — a key-value server with its own protocol

An in-memory key-value store with a Redis-flavoured text protocol, over raw TCP.
Like the [chat](../chat/) server it is concurrent — one `start` thread per
connection — but instead of broadcasting, the connections share a single mutable
store guarded by a `Lock`, and each command line gets a reply. It's the "network
protocol" showcase: no HTTP, just a line protocol you can type by hand.

## Run it

```sh
build/rakupp showcase/kvstore/kvstore.raku          # listens on 127.0.0.1:6380
PORT=7000 build/rakupp showcase/kvstore/kvstore.raku
# or compile a standalone binary:
build/rakupp --exe -o kvstore showcase/kvstore/kvstore.raku && ./kvstore
```

Then connect with any line-oriented TCP client:

```sh
nc 127.0.0.1 6380
```

## A session

```
$ nc 127.0.0.1 6380
rakupp-kv ready. One command per line; HELP for the list.
SET name ada
OK
GET name
ada
SET greeting "hello world"
OK
GET greeting
hello world
INCR hits
1
INCR hits
2
APPEND name " lovelace"
12
GET name
ada lovelace
KEYS
greeting hits name
DBSIZE
3
DEL name
1
GET name
(nil)
QUIT
```

(The short lines are what you type; the line under each is the server's reply.)

## Commands

| Command | Reply |
|---|---|
| `SET key value…` | `OK` — value may be `"quoted to keep spaces"` |
| `GET key` | the value, or `(nil)` |
| `DEL key…` | count of keys removed |
| `EXISTS key` | `1` / `0` |
| `INCR key` / `DECR key` | the new integer value |
| `APPEND key value` | the new string length |
| `KEYS` | all keys, sorted |
| `DBSIZE` | number of keys |
| `FLUSHALL` | `OK` — clears the store |
| `PING [msg]` | `PONG` (or the echoed message) |
| `HELP` | the command list |
| `QUIT` | closes the connection |

## How it works

- **One thread per client.** The accept loop does `start serve($conn)`, so many
  connections are handled at once; a blocking `recv` releases the interpreter lock
  and the others keep flowing.
- **One shared store under a `Lock`.** Every read and write goes through
  `$lock.protect { … }`, so concurrent `INCR`/`SET`/`DEL` stay consistent.
- **Hand-rolled protocol.** A line scanner splits each command into a verb and
  arguments, honouring `"double quotes"` so a value can contain spaces; a
  `given`/`when` dispatches to the handler, which returns the reply string.
