# Toy Forth Interpreter in C

Un interprete minimale in stile **Forth** scritto in C, progettato con un sistema di tipizzazione dinamica, gestione manuale della memoria tramite *reference counting* e supporto per la definizione di nuove parole.

## Caratteristiche Principali

* **Tipizzazione Dinamica:** Supporta interi (`int`), float (`float`), booleani (`bool`), stringhe (`str`), simboli (`symbol`) e liste (`list`).
* **Memory Management:** Sistema di conteggio dei riferimenti (`retain`/`release`) integrato per prevenire memory leak e gestire correttamente le liste e gli oggetti complessi.
* **Dizionario Estensibile:** Supporta la ridefinizione delle parole (*shadowing*) e la creazione di nuove parole a runtime tramite la sintassi `:` ... `;`.
* **Architettura Pulita:** Separazione netta tra la fase di parsing/compilazione (creazione dell'AST) e la fase di esecuzione.

## Requisiti

Per compilare ed eseguire il codice serve un compilatore C standard (es. `gcc` o `clang`) e un sistema operativo compatibile (Linux, macOS o Windows con WSL/MinGW).

## Compilazione

```bash
gcc main.c -o main -O2 -W -Wall 