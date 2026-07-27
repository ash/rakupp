(define-module (rakupp-package)
  #:use-module (guix packages)
  #:use-module (guix gexp)
  #:use-module (guix git-download)
  #:use-module (guix build-system cmake)
  #:use-module ((guix licenses) #:prefix license:))

(package
 (name "rakupp")
 (version "1.2.0")
 (source (origin
          (method git-fetch)
          (uri (git-reference
                (url "https://github.com/ash/rakupp")
                (commit "main")))
          (file-name (git-file-name name version))
          (sha256
           (base32
            "1w4sr6d5fag1a4n9yaavi0ihvizr0prf6050g8mlqfrx5wsrzqm3"))))
 (build-system cmake-build-system)
 (arguments
  (list 
   #:build-type "Release"
   #:tests? #f))
 (synopsis "Raku++ — a Raku language interpreter and compiler written from scratch in C++17, validated against the Roast spec suite.")
 (description
  "A from-scratch implementation of the Raku programming language in C++17, with no third-party dependencies — a hand-written lexer, parser, and tree-walking evaluator that runs real Raku (classes, roles, grammars, regexes, multi-dispatch, junctions, lazy sequences, a bignum tower, Unicode-correct strings, and concurrency), can also compile a program to a standalone native binary, and — as Raku.js — runs in the browser via WebAssembly, no server required. It is not a fork of Rakudo and shares no code with it; it targets the language, measured against Roast, the official Raku test suite.")
 (home-page "https://github.com/ash/rakupp")
 (license license:artistic2.0))
