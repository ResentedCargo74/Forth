#include "forth.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *read_line(FILE *input) {
    char *line = NULL;
    size_t length = 0;
    size_t capacity = 0;
    int character;

    while ((character = fgetc(input)) != EOF && character != '\n') {
        if (length + 1 >= capacity) {
            capacity = capacity == 0 ? 128 : capacity * 2;
            line = forth_realloc(line, capacity);
        }
        line[length++] = (char)character;
    }
    if (character == EOF && length == 0) {
        free(line);
        return NULL;
    }
    if (length + 1 >= capacity)
        line = forth_realloc(line, length + 1);
    line[length] = '\0';
    return line;
}

int forth_repl(Context *ctx) {
    for (;;) {
        Value *program;
        char *line;

        fputs("forth> ", stdout);
        fflush(stdout);
        line = read_line(stdin);
        if (line == NULL) {
            putchar('\n');
            return 0;
        }
        if (strcmp(line, "exit") == 0 || strcmp(line, "quit") == 0) {
            free(line);
            return 0;
        }
        program = forth_compile(line);
        free(line);
        if (program == NULL)
            continue;
        /* A failed line leaves its surviving stack values visible for inspection and recovery. */
        (void)forth_execute(ctx, program);
        value_release(program);
    }
}
