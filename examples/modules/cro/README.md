# A tiny Cro service

A minimal [Cro](https://cro.raku.org) web service, laid out the way `cro stub`
lays one out, that runs under Raku++ with the `cro` development tool.

## Files

| File | What it is |
|------|------------|
| [`service.raku`](service.raku) | The service: two routes and a server that runs until Ctrl-C |
| [`.cro.yml`](.cro.yml) | Tells `cro run` how to start the service and which environment variables carry its host and port |
| [`META6.json`](META6.json) | The project's metadata, naming its dependency on `Cro::HTTP` |

## Routes

| Route | Response |
|-------|----------|
| `GET /` | `Hello from Cro!` |
| `GET /greet/<name>` | `Hello, <name>!` |

## Install

Install Cro, which brings the `cro` tool and `Cro::HTTP`:

```sh
rakupp install cro
```

## Run with `cro run`

From this directory:

```sh
cro run
```

```
▶ Starting Hello (hello)
🔌 Endpoint HTTP will be at http://localhost:20000/
📓 hello Listening at http://localhost:20000
```

`cro run` chooses the port, passes it to the service in `HELLO_HTTP_HOST` and
`HELLO_HTTP_PORT` (the names `.cro.yml` gives), and restarts the service when a
file in the directory changes:

```
♻ Restarting Hello (hello)
  (change to …/examples/modules/cro/service.raku)
📓 hello Listening at http://localhost:20000
```

In another terminal:

```sh
curl http://localhost:20000/
curl http://localhost:20000/greet/Cro
```

```
Hello from Cro!
Hello, Cro!
```

## Run without the `cro` tool

With neither variable set, the service falls back to `localhost:10000`:

```sh
rakupp service.raku
```

```
Listening at http://localhost:10000
```

Stop it with Ctrl-C.
