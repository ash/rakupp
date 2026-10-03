# Emacs: Raku with rakupp

New to Emacs? [EMACS-101.md](EMACS-101.md) starts from installing it and walks
through every feature below, key by key.

With a few lines in your Emacs init file you get four things, on top of
running a file with `M-x compile`:

- **Raku highlighting and indentation** in `.raku`, `.rakumod`, `.rakutest`,
  `.p6` and `.pm6` files, from the `raku-mode` package.
- **Mistakes underlined as you type**: syntax errors, undeclared variables and
  the findings of `rakupp --lint`, each on the variable or operator it is
  about. Emacs's built-in LSP client, eglot, gets them from
  [`rakupp --lsp`](LSP.md).
- **Completion, documentation and go-to-definition** from the same server:
  variables in scope, your subs and classes, and every built-in sub and
  method, with the [reference](../REFERENCE.md) entry for a built-in.
- **A Raku REPL in an Emacs buffer**, `M-x run-raku`, that you can send lines,
  regions and whole files to.

You need Emacs 29 or later, which has eglot and `use-package` built in, and
`rakupp` itself, newer than 5.2.1. With 5.2.1 or older, Emacs still shows the
underlines, on whole lines, but gets no completion, documentation or
definitions, and the REPL buffer shows no `>` prompts (see
[Troubleshooting](#troubleshooting)).

## 1. Add Raku to your init file

Your init file is the first of `~/.emacs.d/init.el`, `~/.config/emacs/init.el`
or `~/.emacs` that exists. If none does, create `~/.emacs.d/init.el`. Add:

```elisp
;;; init.el --- my Emacs setup  -*- lexical-binding: t -*-

(require 'package)
(add-to-list 'package-archives '("melpa" . "https://melpa.org/packages/") t)

(use-package raku-mode
  :ensure t
  :custom (raku-exec-path "rakupp"))

(use-package eglot
  :hook (raku-mode . eglot-ensure)
  :config
  (add-to-list 'eglot-server-programs '(raku-mode . ("rakupp" "--lsp"))))
```

Restart Emacs. The first start downloads `raku-mode` from MELPA, which takes a
few seconds.

The first line must be the very first line of the file. Without it, Emacs 31
opens a `*Warnings*` window about a missing `lexical-binding` cookie at every
start. If your init file already has such a line, keep yours. The two lines
after it are the standard MELPA setup; keep one copy if they are there
already.

If you started Emacs from the Dock, Spotlight or a desktop menu, it may not
see the `PATH` your shell sets up, and so it cannot find `rakupp`. In that case
replace both `"rakupp"` strings with the absolute path that `command -v rakupp`
prints in a terminal.

## 2. Mistakes underlined as you type

Save this as `hello.raku` and open it in Emacs:

```raku
my $unused = 1;
say $undeclared;
sub f { return 42 }
```

The mode line shows `Raku`, and the echo area says
``[eglot] Connected! Server `rakupp-lsp' now managing `(raku-mode)' buffers``.
`$unused`, `$undeclared`, `f` and `return` get underlined. These are the
same four findings that `rakupp --lint hello.raku` prints:

| Line | Severity | Message |
|---|---|---|
| 1 | warning | `rakupp [unused-variable]: '$unused' is declared but never used` |
| 2 | error | `rakupp [undeclared-variable]: '$undeclared' is not declared` |
| 3 | note | `rakupp [redundant-return]: 'return' as the final statement is redundant; the block's last value is returned automatically` |
| 3 | warning | `rakupp [unused-routine]: lexical routine '&f' is never called` |

Move the cursor onto an underlined word and its message appears in the echo
area at the bottom of the frame. Two commands help with the rest:

| Command | What it does |
|---|---|
| `M-x flymake-show-buffer-diagnostics` | Opens a list of every finding in the file. Press `RET` on one to jump to it. |
| `M-x flymake-goto-next-error` / `flymake-goto-prev-error` | Moves to the next or previous finding. |

Edit the file and the underlines update without saving. Change `$undeclared`
to `$unused` on line 2, and the line 1 and line 2 findings both disappear.

[What the server reports](LSP.md#what-the-server-reports) lists every kind of
finding and its severity.

## 3. Completion, documentation and definitions

Save this as `greet.raku` and open it:

```raku
#| Greets someone by name.
sub greet($who) {
    say "Hello, $who!";
}

my $count = 3;
greet('Emacs') for ^$count;
```

| You want | Do this |
|---|---|
| To complete a name | Type its start and press `C-M-i`. In a terminal, press Esc, then Tab. On a new line, `$co` becomes `$count`. When several names fit, a `*Completions*` window lists them: click one, or type more and press `C-M-i` again. |
| To see what a name is | Put the cursor on it. The echo area shows the declaration and its `#|` comment: on `greet`, `sub greet($who)`, `Greets someone by name.` and `Sub, line 2.` On a built-in such as `say`, it shows the reference entry. `C-h .` opens the full text in a window. |
| To jump to a declaration | Put the cursor on a use of the name and press `M-.`. `M-,` jumps back. |

After a `.`, completion offers methods: type `"text".u` and press `C-M-i` to
see `uc`, `uniname` and the other built-in methods that start with `u`.
Methods declared in your file come first.

Completion and documentation cover the open file and the built-ins, not the
modules it `use`s. [LSP.md](LSP.md#hover-completion-and-go-to-definition)
says exactly what they know.

## 4. Run the file

Take this program, saved as `fact.raku` (`C-x C-s` saves):

```raku
sub postfix:<!>($n) {
    [*] 1..$n;
}

say 5!;
```

Run `M-x compile`. Emacs suggests `make -k`; replace it with
`rakupp fact.raku` and press `RET`. The output appears in a `*compilation*`
window:

```
rakupp fact.raku
120

Compilation finished at Sat Oct  3 20:02:00, duration 0.10 s
```

`M-x recompile` runs the same command again without asking. `C-x 1` closes the
other window.

## 5. The REPL

`M-x run-raku` opens a `*Raku REPL*` buffer running `rakupp`. Type at the `>`
prompt and press `RET`:

```
> my $x = 6;
6
> say $x * 7;
42
> sub greet($who) {
*     say "Hello, $who!";
* }
&greet
> greet('Emacs');
Hello, Emacs!
```

A `*` prompt means the statement is not finished yet. Declarations stay alive
for the rest of the session.

From a `.raku` buffer you can send code to the REPL. It starts the REPL first
if it is not running:

| Key | Sends |
|---|---|
| `C-c C-l` | the current line |
| `C-c C-r` | the selected region |
| `C-c C-b` | the whole buffer |

Sent code arrives as if typed line by line, so the REPL prints a prompt for
each line, blank ones included, and the results follow them, for example
`> > 120`.

## Using lsp-mode instead of eglot

If your setup uses `lsp-mode` (Doom Emacs and Spacemacs do by default),
use this block in place of the eglot one. `lsp-mode` has no entry for Raku
of its own, so the block registers `rakupp`:

```elisp
(use-package lsp-mode
  :ensure t
  :hook (raku-mode . lsp-deferred)
  :config
  (add-to-list 'lsp-language-id-configuration '(raku-mode . "raku"))
  (lsp-register-client
   (make-lsp-client :new-connection (lsp-stdio-connection '("rakupp" "--lsp"))
                    :activation-fn (lsp-activate-on "raku")
                    :server-id 'rakupp)))
```

The first time you open a Raku file, `lsp-mode` asks which folder is the
project root. The diagnostics, completion, documentation and `M-.` are the
same as with eglot. In `lsp-mode`'s messages the code comes last, for example
`'$unused' is declared but never used [unused-variable]`.

## Troubleshooting

| What you see | Why, and the fix |
|---|---|
| `Searching for program: No such file or directory, rakupp` | Emacs cannot find `rakupp` on its `PATH`. Put the absolute path in both places (see step 1), or install the `exec-path-from-shell` package so Emacs reads your shell's `PATH`. |
| The mode line says `Fundamental`, not `Raku` | `raku-mode` is not installed. Check `*Messages*` for a MELPA download error, then run `M-x package-refresh-contents` and `M-x package-install RET raku-mode`. |
| The REPL buffer shows results separated by blank lines, with no `>` prompts | The `rakupp` is 5.2.1 or older. It treats Emacs's buffer as a full terminal, and its line-editor redraws erase the prompt. Use a newer `rakupp`. |
| `lsp-mode` reports `method not found: textDocument/diagnostic` | The `rakupp` is 5.2.1 or older. Diagnostics still arrive, and a newer `rakupp` stops the message. |
| Completion finds nothing, and the echo area shows no documentation | The `rakupp` is 5.2.1 or older, which answers diagnostics only. Use a newer `rakupp`, then run `M-x eglot-reconnect`. |
| The underlines did not change after you rebuilt `rakupp` | The server is still the old process. Run `M-x eglot-reconnect`, or `M-x lsp-workspace-restart` with `lsp-mode`. |

To see the messages exchanged with the server, run `M-x eglot-events-buffer`.
To tell an Emacs problem from a server problem, run
[`editors/lsp-demo.sh`](LSP.md#check-the-server-without-an-editor) in a
terminal.

## What it does not do

The server has no find-references (`M-?`), rename or formatting. It looks at
one file at a time, and it does not infer types, so after a `.` it offers
every method it knows rather than the methods of the value's type.
[LSP.md](LSP.md#hover-completion-and-go-to-definition) has the details.

This page was checked with Emacs 31.1, the eglot built into it, `raku-mode`
20250930 and `lsp-mode` 20261002 from MELPA.
