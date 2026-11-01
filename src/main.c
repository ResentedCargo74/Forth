#include "forth.h"

#include <stdio.h>
#include <stdlib.h>

static char *read_file(const char *path) {
    FILE *file;
    char *buffer;
    long size;
    size_t read;

    file = fopen(path, "rb");
    if (file == NULL) {
        perror(path);
        return NULL;
    }
    if (fseek(file, 0, SEEK_END) != 0 || (size = ftell(file)) < 0 || fseek(file, 0, SEEK_SET) != 0) {
        perror(path);
        fclose(file);
        return NULL;
    }
    buffer = forth_alloc((size_t)size + 1);
    read = fread(buffer, 1, (size_t)size, file);
    if (fclose(file) != 0 || read != (size_t)size) {
        fprintf(stderr, "forth: cannot read `%s` completely\n", path);
        free(buffer);
        return NULL;
    }
    buffer[size] = '\0';
    return buffer;
}

/*
 * Entry point: if a `program.tf` path is given, compile+execute it and
 * print the final stack. Otherwise, drop into the REPL (`forth> `).
 * Usage: forth [program.tf].
 */
int main(int argc, char **argv) {
    Context *ctx;
    int result = 0;

    if (argc > 2) {
        fprintf(stderr, "usage: %s [program.tf]\n", argv[0]);
        return EXIT_FAILURE;
    }
    ctx = forth_context_new();
    forth_install_core_words(ctx);
    if (argc == 2) {
        char *source = read_file(argv[1]);
        Value *program;
        if (source == NULL) {
            forth_context_free(ctx);
            return EXIT_FAILURE;
        }
        program = forth_compile(source);
        free(source);
        if (program == NULL) {
            forth_context_free(ctx);
            return EXIT_FAILURE;
        }
        result = forth_execute(ctx, program);
        value_release(program);
        fputs("Stack at end: ", stdout);
        forth_print_value(ctx->stack);
        putchar('\n');
    } else {
        result = forth_repl(ctx);
    }
    forth_context_free(ctx);
    return result == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
