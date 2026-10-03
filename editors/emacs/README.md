# Emacs — init files for rakupp

> **Using it?** The user guide is
> [docs/guide/integrations/EMACS.md](../../docs/guide/integrations/EMACS.md),
> and [EMACS-101.md](../../docs/guide/integrations/EMACS-101.md) starts from
> installing Emacs. This directory holds the same settings as files, and a
> check that runs them.

Emacs support is configuration, not a package: `raku-mode` from MELPA gives
highlighting, indentation and `M-x run-raku`, and Emacs's LSP client talks to
`rakupp --lsp` for diagnostics, completion, hover and go-to-definition.

| File | What it is |
|---|---|
| [`init-eglot.el`](init-eglot.el) | `raku-mode` + eglot, the client built into Emacs 29 and later. The setup the guides use. |
| [`init-lsp-mode.el`](init-lsp-mode.el) | `raku-mode` + `lsp-mode`, for setups built on it (Doom Emacs, Spacemacs). |
| [`check.sh`](check.sh) | Loads `init-eglot.el` into a batch Emacs and checks what eglot gets from the server. |

## Use one of the init files

Copy its lines into your init file (`~/.emacs.d/init.el`), or load it from
there:

```elisp
(load "/path/to/rakupp/editors/emacs/init-eglot.el")
```

Use one of the two files, not both. Both run `rakupp` from Emacs's `PATH`; if
Emacs does not find it, replace the `"rakupp"` strings with the absolute path
that `command -v rakupp` prints.

## Check the setup

```bash
cd editors/emacs && RAKUPP=../../build/rakupp ./check.sh
```

```
ok    major mode: raku-mode
ok    eglot server: rakupp --lsp connected
ok    diagnostics: 5:unused-variable 6:undeclared-variable 9:undeclared-variable
ok    completion of $co: ($count)
ok    hover on greet: sub greet($who) Greets someone by name. Sub, line 2.
PASS
```

It runs Emacs with `--init-directory` set to a throwaway directory
(`$RAKUPP_EMACS_HOME`, default `$TMPDIR/rakupp-emacs-check`), so your own
`~/.emacs.d` is never read or written. The first run downloads `raku-mode`
from MELPA into it; later runs reuse it. `RAKUPP` picks the binary (default:
`rakupp` on `PATH`), `EMACS` the Emacs (default: `emacs`, version 29 or
later). The exit status is 0 on PASS and 1 when a check fails; Emacs's own
messages go to `emacs.log` in the throwaway directory.

A `rakupp` 5.2.1 or older passes the first three checks and fails completion
and hover, which it does not answer.

The check covers `init-eglot.el` only. `lsp-mode` starts its client when the
buffer becomes visible, which never happens in batch mode.
