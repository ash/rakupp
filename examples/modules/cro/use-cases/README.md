# Cro use cases

Four programs that each start a [Cro](https://cro.raku.org) server, talk to it
with Cro's own client in the same process, and print what came back. They cover
what a real service does beyond a hello-world route: JSON APIs, forms and
uploads, middleware, templates, streaming, and WebSockets. Each one prints the
same thing on every run, and [`check.raku`](check.raku) compares that output
with the copy kept in [`expected/`](expected), so the set doubles as a test of
Cro under whichever Raku runs it.

| Program | What it does |
|---------|--------------|
| [`rest-api.raku`](rest-api.raku) | A JSON book store: path segments typed and untyped, query parameters, JSON in and out, `created`, `not-found`, a redirect, request headers, a teapot, a handler that dies, and a cookie jar |
| [`forms.raku`](forms.raku) | One route answering a urlencoded form and a multipart one, the fields taken apart in the handler's signature; a missing required field; escaping; a file upload |
| [`web-features.raku`](web-features.raku) | `before`/`after` middleware, `include` under a prefix, static files, a Cro::WebApp template, a streamed response, server-sent events, three slow requests served in parallel, a megabyte each way |
| [`websocket-chat.raku`](websocket-chat.raku) | An echo endpoint for text and binary frames, then a JSON chat room with three clients: joins, broadcasts, a leave, and the log each client kept |

The static file and the template that `web-features.raku` serves are in
[`static/`](static) and [`templates/`](templates).

## Install

```sh
rakupp install cro
```

`cro` brings Cro::HTTP and Cro::WebSocket; `web-features.raku` also needs
Cro::WebApp:

```sh
rakupp install Cro::WebApp
```

## Run one

```sh
rakupp rest-api.raku
```

Each program listens on its own port on 127.0.0.1 (20301 to 20304), or on the
port in `CRO_EXAMPLE_PORT`, and stops its server when it finishes.

## Check them all

```sh
rakupp check.raku
```

A use case passes when its standard output matches `expected/NAME.out` line
for line. Name some to run only those (`rakupp check.raku forms`), and pass
`--engine=PATH` to check another binary. Running the checker with `rakudo`
checks Rakudo against the same files.
