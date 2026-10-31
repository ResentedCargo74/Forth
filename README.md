# Forth

A small, embeddable Forth-style interpreter in C11. The project favours plain data
structures, visible ownership rules, and a short execution path over a framework.

## Build and test

```sh
make
make test
./forth program.tf
```

On Windows with MinGW, use `mingw32-make`, or compile directly:

```sh
gcc -std=c11 -O2 -Wall -Wextra -Wpedantic -Iinclude -o forth.exe src/main.c src/value.c src/dictionary.c src/parser.c src/vm.c src/repl.c
```

Run without an argument for the REPL. Type `quit` or `exit` to leave it.

## Language overview

Values are integers, floating-point values, booleans, strings, symbols, and the
internal lists used for programs. `true` and `false` are real boolean literals.
Numbers accept scientific notation; strings accept `\n`, `\r`, `\t`, `\\`, and `\"`.

```forth
\ A line comment
( A parenthesized comment )

: square dup * ;
: factorial dup 1 > if dup 1 - factorial * else drop 1 then ;

5 factorial . cr
3 begin dup 0 > while dup . 1 - repeat drop
```

Core words include:

| Group | Words |
| --- | --- |
| Arithmetic | `+ - * / mod /mod negate abs 1+ 1- 2* 2/ min max` |
| Comparisons | `= <> < > <= >= 0= 0<> 0< 0>` |
| Bit and flags | `and or xor invert lshift rshift not` |
| Data stack | `dup ?dup drop swap over rot -rot nip tuck pick roll` |
| Double-cell stack | `2dup 2drop 2swap 2over` |
| Return stack | `>r r> r@` |
| Memory | `variable constant value to @ ! +! ?` |
| Output and discovery | `. .s ." s" cr emit words` |
| Flow control | `if ... else ... then`, `begin ... while ... repeat`, `do/?do ... loop/+loop`, `i j leave` |
| Definitions | `: name ... ;` |

The operands remain on the stack after a type or arithmetic error. This makes an
interactive session inspectable instead of silently discarding the evidence.

## Project map

- `include/forth.h` is the compact public API and ownership contract.
- `src/value.c` owns dynamic values, reference counting, lists, and printing.
- `src/dictionary.c` owns contexts, stacks, and the append-only word dictionary.
- `src/parser.c` turns source into a program list.
- `src/vm.c` provides the core words and evaluator.
- `src/repl.c` provides unbounded interactive input; `src/main.c` is only the CLI shell.
- `tests/test_forth.c` is a small API-level regression suite.

Read [ARCHITECTURE.md](ARCHITECTURE.md) before extending the interpreter: it records
the current invariants, decisions, and changes made to the project.

The behaviour of the standard-style words above follows the
[Gforth 1.0 manual](https://gforth.org/manual/1.0/), notably its documentation of
[integer division](https://gforth.org/manual/1.0/Integer-division.html),
[counted loops](https://gforth.org/manual/1.0/Counted-Loops.html), and
[defining words](https://gforth.org/manual/1.0/Constants.html). This is a compact
core interpreter, not a full Gforth distribution: files, block I/O, compiler macros,
and Gforth-specific object systems remain intentionally out of scope.
