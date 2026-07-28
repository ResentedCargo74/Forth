# Toy Forth Interpreter in C

A minimalist **Forth**-style interpreter written in C, designed with a dynamic typing system, manual memory management via *reference counting*, and support for defining new words.

## Main Features

* **Dynamic Typing:** Supports integers (`int`), floats (`float`), booleans (`bool`), strings (`str`), symbols (`symbol`), and lists (`list`).
* **Memory Management:** Integrated reference counting system (`retain`/`release`) to prevent memory leaks and correctly manage lists and complex objects.
* **Extensible Dictionary:** Supports word redefinition (*shadowing*) and runtime creation of new words using the `:` ... `;` syntax.
* **Clean Architecture:** Clear separation between the parsing/compilation phase (AST creation) and the execution phase.

## Requirements

To compile and run the code, you need a standard C compiler (e.g., `gcc` or `clang`) and a compatible operating system (Linux, macOS, or Windows with WSL/MinGW).

## Compilation

```bash
gcc main.c -o main -O2 -W -Wall