#!/usr/bin/env bash
# Load init-eglot.el into a batch Emacs with a throwaway init directory, open
# a Raku file, and print what eglot got from `rakupp --lsp`: the diagnostics,
# a completion and a hover. Your own ~/.emacs.d is never read or written.
#
#   ./check.sh                        # uses `rakupp` from PATH
#   RAKUPP=../../build/rakupp ./check.sh
#
# The first run downloads raku-mode from MELPA into the throwaway directory
# ($RAKUPP_EMACS_HOME, default $TMPDIR/rakupp-emacs-check); later runs reuse it.
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
RAKUPP="${RAKUPP:-rakupp}"
EMACS="${EMACS:-emacs}"
TMP="${TMPDIR:-/tmp}"
HOME_DIR="${RAKUPP_EMACS_HOME:-${TMP%/}/rakupp-emacs-check}"

# init-eglot.el runs plain "rakupp": put the chosen binary first on PATH under
# that name.
BIN="$(command -v "$RAKUPP" || true)"
[[ -n "$BIN" ]] || { echo "check.sh: no rakupp at '$RAKUPP'" >&2; exit 2; }
mkdir -p "$HOME_DIR/bin"
ln -sf "$(cd "$(dirname "$BIN")" && pwd)/$(basename "$BIN")" "$HOME_DIR/bin/rakupp"
export PATH="$HOME_DIR/bin:$PATH"

cat > "$HOME_DIR/check.raku" <<'RAKU'
#| Greets someone by name.
sub greet($who) {
    say "Hello, $who!";
}
my $unused = 1;
say $undeclared;
my $count = 3;
greet('Emacs') for ^$count;
$co
RAKU

cat > "$HOME_DIR/check.el" <<'ELISP'
;;; check.el  -*- lexical-binding: t -*-
(defvar check-failed nil)
(defun check (ok what got)
  (princ (format "%s  %s: %s\n" (if ok "ok  " "FAIL") what got))
  (unless ok (setq check-failed t)))
(defun check-wait (secs)
  (let ((end (+ (float-time) secs)))
    (while (< (float-time) end) (accept-process-output nil 0.1))))

(find-file (expand-file-name "check.raku" user-emacs-directory))
(run-hooks 'post-command-hook)          ; eglot-ensure waits for a command
(check-wait 3)                          ; let every diagnostic arrive
(check (eq major-mode 'raku-mode) "major mode" major-mode)
(check (eglot-current-server) "eglot server"
       (if (eglot-current-server) "rakupp --lsp connected" "none"))

(let ((codes (mapcar (lambda (d)
                       (format "%d:%s" (line-number-at-pos (flymake-diagnostic-beg d))
                               (and (string-match "\\[\\([-a-z]+\\)\\]" (flymake-diagnostic-text d))
                                    (match-string 1 (flymake-diagnostic-text d)))))
                     (flymake-diagnostics))))
  (setq codes (sort codes #'string<))
  (check (equal codes '("5:unused-variable" "6:undeclared-variable" "9:undeclared-variable"))
         "diagnostics" (string-join codes " ")))

(goto-char (point-max)) (skip-chars-backward "\n")
(let* ((capf (and (eglot-current-server) (eglot-completion-at-point)))
       (cands (and capf (all-completions "$co" (nth 2 capf)))))
  (check (member "$count" cands) "completion of $co" cands))

(goto-char (point-min)) (search-forward "greet('") (backward-char 7)
(let* ((hover (condition-case err
                    (jsonrpc-request (eglot-current-server) :textDocument/hover
                                     (eglot--TextDocumentPositionParams))
                  (error (list :contents (list :value (format "%S" (cdr err)))))))
       (text (if hover
                 (replace-regexp-in-string
                  "\n+" " " (plist-get (plist-get hover :contents) :value))
               "none")))
  (check (string-match-p "Greets someone by name" text) "hover on greet" text))

(princ (if check-failed "FAIL\n" "PASS\n"))
(kill-emacs (if check-failed 1 0))
ELISP

# Emacs's own chatter (MELPA download, raku-mode's compile warnings, eglot's
# messages) goes to stderr; keep it in a log, print the checks.
"$EMACS" --batch --init-directory="$HOME_DIR" \
    -f package-initialize -l "$HERE/init-eglot.el" -l "$HOME_DIR/check.el" \
    2> "$HOME_DIR/emacs.log" || {
  status=$?
  echo "Emacs log: $HOME_DIR/emacs.log" >&2
  exit "$status"
}
