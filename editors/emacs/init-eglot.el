;;; init-eglot.el --- Raku with rakupp: raku-mode + eglot  -*- lexical-binding: t -*-

;; Copy these lines into your init file (~/.emacs.d/init.el), or load this
;; file from it:  (load "/path/to/rakupp/editors/emacs/init-eglot.el")
;; Needs Emacs 29 or later. Guide: docs/guide/integrations/EMACS.md

(require 'package)
(add-to-list 'package-archives '("melpa" . "https://melpa.org/packages/") t)

(use-package raku-mode
  :ensure t
  :custom (raku-exec-path "rakupp"))

(use-package eglot
  :hook (raku-mode . eglot-ensure)
  :config
  (add-to-list 'eglot-server-programs '(raku-mode . ("rakupp" "--lsp"))))

;;; init-eglot.el ends here
