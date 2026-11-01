\ Demo program showing the interpreter's capabilities.

\ Recursive factorial
: fact dup 1 > if dup 1 - fact * else drop 1 then ;

\ Floating-point arithmetic and strings
1.5 2.5 + .
s" hello world" .

\ Variables: store, fetch, add-to
variable x
10 x !
x @
3 x +!
x @

\ Constants and values
42 CONSTANT ANSWER
ANSWER
7 value N
N
2 N + to N
N

\ Counted loop printing 1..3
: countup 3 0 ?do i . loop ;
countup

\ Boolean logic (bitwise on integer flags)
1 0 and .
0 1 or .
true not .

\ Stack manipulation
1 2 3 over over rot .s drop

cr