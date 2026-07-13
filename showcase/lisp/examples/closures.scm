; Lexical closures: make-adder returns a function that closed over n.
(define (make-adder n) (lambda (x) (+ x n)))
(define add5 (make-adder 5))
(define add100 (make-adder 100))
(display "add5(10)   = ") (display (add5 10))   (newline)
(display "add100(10) = ") (display (add100 10)) (newline)

; A counter with private mutable state via set!.
(define (make-counter)
  (define count 0)
  (lambda () (set! count (+ count 1)) count))
(define c (make-counter))
(display "counter: ")
(display (c)) (display " ") (display (c)) (display " ") (display (c))
(newline)

; Higher-order: fold and map built on the primitives.
(define (sum lst) (fold-left + 0 lst))
(display "sum 1..10 = ") (display (sum '(1 2 3 4 5 6 7 8 9 10))) (newline)

(define (squares lst) (map (lambda (x) (* x x)) lst))
(display "squares   = ") (display (squares '(1 2 3 4 5))) (newline)

; Mutual recursion.
(define (even? n) (if (= n 0) #t (odd?  (- n 1))))
(define (odd?  n) (if (= n 0) #f (even? (- n 1))))
(display "even? 10  = ") (display (even? 10)) (newline)
(display "odd?  7   = ") (display (odd? 7))   (newline)
