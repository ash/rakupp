(define-module (rakupp-package)
  #:use-module (guix)
  #:use-module (guix packages)
  #:use-module (guix gexp)
  #:use-module (guix git-download)
  #:use-module (guix build-system cmake)
  #:use-module ((guix licenses) #:prefix license:))

(define vcs-file?
  ;; Return true if the given file is under version control. git-predicate
  ;; enumerates the checkout through libgit2, and on some checkouts (the
  ;; shallow depth-1 clone GitHub Actions makes, notably) that enumeration
  ;; comes back empty — the predicate then rejects EVERY file and the package
  ;; gets an empty source tree (CI run 30534305456 failed exactly so, with
  ;; cmake finding no CMakeLists.txt). So probe the predicate against a file
  ;; that is certainly tracked; if it rejects it, fall back to accepting
  ;; everything, which on a pristine checkout is precisely right.
  (let* ((root (dirname (dirname (current-source-directory))))
         (pred (git-predicate root)))
    (if (and pred
             (false-if-exception
              (let ((probe (string-append root "/CMakeLists.txt")))
                (pred probe (lstat probe)))))
        pred
        (const #t))))

(define-public rakupp
  (package
   (name "rakupp")
   ;; Tracks project(RakuPP VERSION …) in CMakeLists.txt — the source is the
   ;; live checkout, so the suffix marks it as a snapshot, not a release.
   (version "3.0.1-git")
   (source (local-file "../.." "rakupp-checkout"
                       #:recursive? #t
                       #:select? vcs-file?))
   (build-system cmake-build-system)
   (arguments
    (list 
     #:build-type "Release"
     #:configure-flags #~(list "-DCMAKE_POSITION_INDEPENDENT_CODE=ON")
     #:tests? #f))
   ;; guix lint conventions: short synopsis, no trailing period.
   (synopsis "Raku language interpreter and compiler written from scratch in C++17")
   (description
    "A from-scratch implementation of the Raku programming language in C++17, with no third-party dependencies — a hand-written lexer, parser, and tree-walking evaluator that runs real Raku (classes, roles, grammars, regexes, multi-dispatch, junctions, lazy sequences, a bignum tower, Unicode-correct strings, and concurrency), can also compile a program to a standalone native binary, and — as Raku.js — runs in the browser via WebAssembly, no server required. It is not a fork of Rakudo and shares no code with it; it targets the language, measured against Roast, the official Raku test suite.")
   (home-page "https://github.com/ash/rakupp")
   (license license:artistic2.0)))

rakupp
