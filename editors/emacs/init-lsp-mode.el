;;; init-lsp-mode.el --- Raku with rakupp: raku-mode + lsp-mode  -*- lexical-binding: t -*-

;; The lsp-mode alternative to init-eglot.el, for setups built on lsp-mode
;; (Doom Emacs, Spacemacs). Use one of the two files, not both.
;; Guide: docs/guide/integrations/EMACS.md#using-lsp-mode-instead-of-eglot

(require 'package)
(add-to-list 'package-archives '("melpa" . "https://melpa.org/packages/") t)

(use-package raku-mode
  :ensure t
  :custom (raku-exec-path "rakupp"))

(use-package lsp-mode
  :ensure t
  :hook (raku-mode . lsp-deferred)
  :config
  (add-to-list 'lsp-language-id-configuration '(raku-mode . "raku"))
  (lsp-register-client
   (make-lsp-client :new-connection (lsp-stdio-connection '("rakupp" "--lsp"))
                    :activation-fn (lsp-activate-on "raku")
                    :server-id 'rakupp)))

;;; init-lsp-mode.el ends here
