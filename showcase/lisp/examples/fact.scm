; Factorials — exact bignums, courtesy of Raku's own Int under the hood.
(define (fact n)
  (if (= n 0) 1 (* n (fact (- n 1)))))

(define (show n)
  (display n) (display "! = ") (display (fact n)) (newline))

(for-each show '(5 10 20 50 100))
