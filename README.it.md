# Forth

Un piccolo interprete Forth in C11, adatto anche a essere incorporato in un altro
programma. Privilegia strutture dati semplici, ownership esplicita e un percorso di
esecuzione breve.

## Compilazione e test

```sh
make
make test
./forth program.tf
```

Su Windows con MinGW è possibile usare `mingw32-make`, oppure:

```sh
gcc -std=c11 -O2 -Wall -Wextra -Wpedantic -Iinclude -o forth.exe src/main.c src/value.c src/dictionary.c src/parser.c src/vm.c src/repl.c
```

Senza argomenti viene avviata la REPL; `quit` ed `exit` la terminano.

## Linguaggio

Sono supportati interi, numeri floating-point, booleani, stringhe e simboli.
`true` e `false` sono letterali booleani effettivi; le stringhe supportano `\n`,
`\r`, `\t`, `\\` e `\"`. I commenti sono `\\` fino a fine riga oppure `( ... )`.

```forth
: square dup * ;
: factorial dup 1 > if dup 1 - factorial * else drop 1 then ;

5 factorial . cr
3 begin dup 0 > while dup . 1 - repeat drop
```

Le parole principali sono:

- aritmetica: `+ - * / mod /mod negate abs 1+ 1- 2* 2/ min max`
- confronti: `= <> < > <= >= 0= 0<> 0< 0>`
- bit e flag: `and or xor invert lshift rshift not`
- data stack: `dup ?dup drop swap over rot -rot nip tuck pick roll`, più
  `2dup 2drop 2swap 2over`
- return stack: `>r r> r@`
- memoria: `variable constant value to @ ! +! ?`
- output: `. .s ." s" cr emit`; ispezione: `words`
- controllo: `if ... else ... then`, `begin ... while ... repeat`,
  `do/?do ... loop/+loop`, `i j leave`
- definizioni: `: nome ... ;`

In presenza di un errore di tipo o aritmetico, gli operandi rimangono nello stack:
la REPL può quindi essere ispezionata e recuperata senza perdere dati.

## Struttura

- `include/forth.h`: API e contratto di ownership.
- `src/value.c`: valori, reference counting, liste e rendering.
- `src/dictionary.c`: contesto, stack e dizionario delle parole.
- `src/parser.c`: analisi del sorgente in una lista di programma.
- `src/vm.c`: primitive ed esecutore.
- `src/repl.c` e `src/main.c`: interfaccia testuale e CLI.
- `tests/test_forth.c`: test di regressione.

La semantica delle parole standard segue il [manuale ufficiale Gforth
1.0](https://gforth.org/manual/1.0/), in particolare le sezioni su [divisione
intera](https://gforth.org/manual/1.0/Integer-division.html), [loop
contati](https://gforth.org/manual/1.0/Counted-Loops.html) e [parole di
definizione](https://gforth.org/manual/1.0/Constants.html). Questo è un nucleo
compatto, non una distribuzione Gforth completa: file, block I/O, macro del
compilatore e object system Gforth restano fuori dallo scopo.
