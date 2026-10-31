# Architecture record

This document is the living architectural record for this repository. Consult it
before changing the interpreter, and update it whenever a change affects an
invariant, module boundary, language feature, or compatibility decision.

## Design in one sentence

Source becomes a reference-counted flat program list; the VM walks that list and
dispatches each symbol through an append-only dictionary, while values move through
the data stack by explicit ownership transfer.

## Module boundaries

| Module | Responsibility | Must not own |
| --- | --- | --- |
| `include/forth.h` | Stable data model and public contracts | VM implementation details |
| `src/value.c` | Values, list capacity, references, textual rendering | Context lifetime |
| `src/dictionary.c` | Context, stacks, dictionary storage | Parsing and word semantics |
| `src/parser.c` | Lexing source and building program lists | Executing source |
| `src/vm.c` | Built-ins, structural words, dispatch and execution limits | CLI/file I/O |
| `src/repl.c` / `src/main.c` | Input and process boundary | Interpreter semantics |

The separation is intentionally boring: modules communicate through `forth.h`, not
through hidden global state. New host integrations can create a `Context`, install
their words, compile source, and execute it without using the command-line program.

## Invariants

1. Every `Value` begins with one owned reference. `value_list_append` and
   `forth_stack_push` consume exactly one owned reference; `forth_stack_pop` returns
   it to the caller. Borrowed values must be retained before being pushed.
2. Lists and dictionary arrays grow geometrically. Do not reintroduce reallocations
   for every pushed value: that turns normal Forth programs quadratic.
3. A dictionary is append-only. Lookup proceeds from its end, so redefining a word
   shadows its older definition without invalidating a running word pointer.
4. A compiled definition retains its source tokens. It therefore remains valid after
   the top-level program list has been released.
5. Built-ins either finish their stack mutation or restore consumed operands on
   recoverable argument errors. This is deliberate REPL behaviour.
6. `forth_execute` is the sole public evaluator and enforces the nesting limit. New
   control flow or callbacks must execute nested programs through it or the bounded
   internal range executor.
7. A `VALUE_CELL` is an address owned by a `VARIABLE` word. Its contained `Value` is
   replaced only by `!` or `+!`; `@` returns a retained copy. Never expose a raw C
   pointer as a Forth address.
8. `WORD_CONSTANT`, `WORD_VALUE`, and `WORD_VARIABLE` retain their payload in the
   dictionary. Constants and values push that payload; variables push their cell.
   `TO` may replace only a `WORD_VALUE` payload.
9. The return stack and counted-loop index stack are separate. `>R`, `R>`, and `R@`
   cannot corrupt loop indices; `I` and `J` only read the explicit loop stack.

## Parsing and language choices

- Whitespace separates tokens. `\\` starts a line comment and `( ... )` is a
  parenthesized comment; comments are removed before the VM sees tokens.
- The parser recognizes full numeric tokens with `strtol`/`strtod`, so `-2`, `.5`,
  and `1e3` work without arbitrary token-size buffers.
- `true` and `false` compile directly to boolean values. This corrects the previous
  mismatch between the documented type system and executable language.
- `if ... else ... then` and `begin ... while ... repeat` are structural VM words.
  The VM scans matching delimiters with nesting awareness and executes only the
  selected range. Unmatched or orphaned delimiters are errors, never unknown words.
- Definitions use `: name ... ;`; their bodies are program lists, not copied strings.
- Symbols are canonicalized to lowercase during parsing, following Forth’s normal
  case-insensitive source convention. Strings retain their spelling exactly.
- `." text"` emits text and `s" text"` pushes text. One delimiter space after the
  opening quote is ignored in the standard Forth spelling (`s" hello"`).
- Counted loops use `limit start do ... loop`; `?do` skips equal bounds, `i`/`j`
  inspect loop indices, `leave` exits the closest counted loop, and `+loop` consumes
  its integer increment. Arithmetic overflow is diagnosed rather than wrapped.

## Added capabilities and fixes

The modular rewrite introduced the following improvements:

- Split the former monolithic `main.c` into six focused C modules and a public header.
- Replaced fixed parser buffers and line limits with dynamically growing storage.
- Added string escape decoding and both standard Forth comment forms.
- Fixed boolean literal support and strengthened syntax diagnostics.
- Added arithmetic remainder, boolean logic, `over`, `rot`, `clear`, `depth`, `cr`,
  `emit`, and `words`.
- Added integer-overflow and zero-divisor checks, an execution-depth guard for runaway
  recursion, and defined error recovery for stack operands.
- Added a Makefile and an API-level regression test covering math, booleans,
  definitions, loops, stack rotation, and comments.
- Removed debug AST printing from file execution; the command-line program now reports
  only the final stack.
- Reorganized implementation files into `include/` and `src/`, leaving root-level
  files for build, documentation, examples, and licensing only.
- Added the classic core stack words (`?DUP`, `2DUP`, `2DROP`, `2SWAP`, `2OVER`,
  `NIP`, `TUCK`, `-ROT`, `PICK`, `ROLL`), return-stack words, integer/bit words,
  expanded comparisons, counted loops, and `LEAVE`.
- Added defining and memory words: `CONSTANT`, `VARIABLE`, `VALUE`, `TO`, `@`, `!`,
  `+!`, and `?`; added `S"` and `."` strings.
- Based the newly added standard-style word behaviour on the Gforth 1.0 manual:
  [integer division](https://gforth.org/manual/1.0/Integer-division.html),
  [counted loops](https://gforth.org/manual/1.0/Counted-Loops.html), and
  [constants](https://gforth.org/manual/1.0/Constants.html).

## Extension guide

To add a primitive, implement a `BuiltinFn` in `vm.c`, preserve the ownership and
error-recovery invariants above, then register it in `forth_install_core_words`.
To add a compound language feature, first decide whether it is a normal word or a
structural word; only structural words should inspect future program tokens. Add a
focused regression test before exposing it in the README.

The coding style stays deliberately compact: clear data paths, a few explicit helper
functions, no abstraction without a consumer, and comments that explain a constraint
or consequence rather than restating a line of C.
