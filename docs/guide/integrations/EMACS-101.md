# Emacs 101 for Raku++

This page is for you if you have never used Emacs. In about twenty minutes you
will install it, connect it to Raku++, and try every feature it gets from
`rakupp`: mistakes underlined as you type, completion, documentation, jumping
to a definition, running a program, and a REPL. Every step says what to type
and what you should see.

[EMACS.md](EMACS.md) is the reference version of this page for people who
already use Emacs.

## How to read Emacs keys

Emacs documentation writes keys in a short form. You need six of them:

| Written | Means |
|---|---|
| `C-x` | Hold Control and press `x`. |
| `C-x C-s` | Two keys in a row: `C-x`, then `C-s`. You may keep Control held down. |
| `M-x` | Meta and `x`. In a terminal, press **Esc**, let go, then press `x`. |
| `C-M-i` | Control, Meta and `i` together. In a terminal, press **Esc**, let go, then press **Tab**: Tab is the same key as `C-i`, and Esc adds the Meta. |
| `RET` | The Enter key. |
| `C-g` | Cancel. When Emacs is asking something you did not mean to start, press `C-g`, more than once if needed. |

The Emacs screen has four parts. The **menu bar** is the top line. The
**buffer** is the large area holding your file. The **mode line** is the
highlighted bar under it: it shows the file name and, in parentheses, what
Emacs knows about the file, such as `Raku`. The **echo area** is the bottom
line, where Emacs prints messages and asks questions.

## 1. Install Emacs

On macOS, with [Homebrew](https://brew.sh):

```bash
brew install emacs
```

On Linux, install your distribution's `emacs` package. Check the version, since
this page needs Emacs 29 or newer:

```bash
emacs --version
```

The first line should read `GNU Emacs 29` or a higher number.

You also need `rakupp`, newer than 5.2.1. [INSTALL.md](../INSTALL.md) has every
way to get it. Check it with:

```bash
rakupp --version
```

## 2. Connect Emacs to Raku++

Emacs reads its settings from a file called the **init file**. On a computer
that has never run Emacs there is none, so check first:

```bash
ls ~/.emacs.d/init.el
```

If that prints `No such file or directory`, create the file with this command.
Copy all of it into a terminal at once:

```bash
mkdir -p ~/.emacs.d && cat > ~/.emacs.d/init.el <<'EOF'
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
EOF
```

If the file already exists, someone has set up Emacs on this computer before;
add the lines between `(require 'package)` and the end to it instead, as
[EMACS.md](EMACS.md#1-add-raku-to-your-init-file) explains.

What the settings do: the first block tells Emacs where to download packages
(MELPA, the main package archive). The second installs `raku-mode`, which
colours Raku code, and tells it to run code with `rakupp`. The third makes
eglot, the part of Emacs that talks to language servers, start
`rakupp --lsp` for every Raku file.

## 3. Open a Raku file

Make a folder to work in and create a file with a mistake in it:

```bash
mkdir -p ~/emacs-101 && cd ~/emacs-101 && cat > tour.raku <<'EOF'
#| Greets someone by name.
sub greet($who) {
    say "Hello, $who!";
}

my $count = 3;
greet('Emacs') for ^$count;
say $total;
EOF
```

Open it in Emacs:

```bash
emacs tour.raku
```

The first time, Emacs downloads `raku-mode`, which takes a few seconds, and
then shows the file in colour. While it installs, a window may open with
warnings such as ``Warning: `point-at-bol' is an obsolete function``. They are
about `raku-mode`'s own code, they are harmless, and they appear only once.
Press `C-x 1` to close that window. The mode line shows `Raku`, and the echo area
says ``[eglot] Connected! Server `rakupp-lsp' now managing `(raku-mode)' buffers``.
Emacs is now connected to Raku++.

Move around with the arrow keys. `C-x C-s` saves the file. `C-x C-c` quits
Emacs. If it asks whether to save or to kill running processes, answer `y` or
type `yes` and press `RET`.

## 4. A tour of what Raku++ adds

### Mistakes are underlined

`$total` on the last line is underlined in red: nothing declares it. Move the
cursor onto it with the arrow keys and read the echo area:

```
rakupp [undeclared-variable]: '$total' is not declared
```

Fix it: replace `$total` with `$count`. Use Backspace to delete and type the
new name. The underline disappears as you type, without saving.

To see every finding in a file at once, press `M-x`, type
`flymake-show-buffer-diagnostics` and press `RET`. A list opens in a second
window. `C-x o` moves the cursor between windows, and `C-x 1` closes all but
the one you are in.

### Documentation for the name under the cursor

Put the cursor on `greet` on line 7. The echo area shows what it is:

```
sub greet($who)
Greets someone by name.
Sub, line 2.
```

The second line is the `#|` comment above the sub. Now put the cursor on
`say` on line 3. For a built-in, the echo area shows its entry from the
[Raku++ reference](../REFERENCE.md):

```
say — .gist + newline · say 42 → 42
(Built-in subroutines › Output & I/O)
```

`C-h .` opens the same text in its own window, which helps when it is long.
Press `q` there to close it.

### Completion

Press `M->` (Esc, then `>`) to go to the end of the file, press `RET` for a
new line, and type `$co`. Now press `C-M-i`: in a terminal, press **Esc**,
then **Tab**. Emacs completes `$co` to `$count`, the only variable that fits.

Completion knows your variables and subs, the variables Raku itself provides
(`$*OUT`, `@*ARGS`), and every built-in sub and method. Delete the line you
typed, then type `"text".u` and press Esc, Tab. A `*Completions*` window
lists the methods that start with `u`: `uc`, `uniname` and more. Type more
letters and press Esc, Tab again to narrow the list, or press `C-g` to stop.
Delete the line again before you go on.

### Jump to a definition

Put the cursor on `greet` on line 7 and press `M-.` (Esc, then `.`). The
cursor jumps to line 2, where `greet` is declared. `M-,` (Esc, then `,`)
jumps back.

### Run the program

Save with `C-x C-s`. Press `M-x`, type `compile` and press `RET`. The echo
area offers `make -k`. Delete it with Backspace, type `rakupp tour.raku` and
press `RET`. A second window shows the output:

```
Hello, Emacs!
Hello, Emacs!
Hello, Emacs!
3
```

To run it again later, use `M-x recompile`. `C-x 1` closes the output window.

### A REPL beside your code

Press `C-c C-b`. Emacs starts the `rakupp` REPL in a second window and sends
it the whole file, so it prints the output again and remembers everything the
file declares. The file arrives line by line, and the REPL prints a prompt for
each line, so prompts pile up as `> > `. That is normal.

Press `C-x o` to move into the REPL window, type `greet('REPL')` at the end
of the last line and press `RET`. The REPL answers:

```
Hello, REPL!
```

`C-c C-l` sends only the current line, and `C-c C-r` the selected region.

## Cheat sheet

| Keys | What it does |
|---|---|
| `C-x C-s` | Save the file |
| `C-x C-c` | Quit Emacs |
| `C-g` | Cancel whatever Emacs is doing or asking |
| `C-/` | Undo |
| `M-x` *command* `RET` | Run any command by name |
| `C-x o` / `C-x 1` | Move to the other window / keep only this one |
| `M-x flymake-show-buffer-diagnostics` | List every finding in the file |
| `C-h .` | Documentation for the name under the cursor, in a window |
| `C-M-i` (Esc, Tab) | Complete the name you are typing |
| `M-.` / `M-,` | Jump to a definition / jump back |
| `M-x compile` | Run a command, such as `rakupp tour.raku` |
| `C-c C-b` / `C-c C-l` / `C-c C-r` | Send the file / line / region to the REPL |

If something does not work, [EMACS.md](EMACS.md#troubleshooting) lists what can
go wrong and how to fix it.
