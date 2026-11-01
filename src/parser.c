#include "forth.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *source;
    const char *cursor;
    int trim_forth_string_space;
} Parser;

static void parser_error(const Parser *parser, const char *message) {
    fprintf(stderr, "forth: syntax error near `%.24s`: %s\n", parser->cursor, message);
}

/*
 * Advance the cursor past whitespace and comments (`\` line comments
 * and `( ... )` nested parenthetical comments). Returns 0 on success,
 * -1 if an unterminated comment is encountered.
 */
static int parser_skip_layout(Parser *parser) {
    for (;;) {
        while (isspace((unsigned char)*parser->cursor))
            parser->cursor++;
        if (*parser->cursor == '\\') {
            while (*parser->cursor != '\0' && *parser->cursor != '\n')
                parser->cursor++;
            continue;
        }
        if (*parser->cursor == '(') {
            int depth = 1;
            parser->cursor++;
            while (*parser->cursor != '\0' && depth > 0) {
                if (*parser->cursor == '(')
                    depth++;
                else if (*parser->cursor == ')')
                    depth--;
                parser->cursor++;
            }
            if (depth > 0) {
                parser_error(parser, "unterminated comment");
                return -1;
            }
            continue;
        }
        return 0;
    }
}

/*
 * Check whether `text` of `length` characters is a numeric token and,
 * if so, store the allocated Value* at `*result` (caller owns a ref).
 * Ints have no dot/e/E; floats do. The whole token must parse; trailing
 * garbage causes a miss. This intentionally does NOT accept hex etc. —
 * those become symbols. Integer overflow sets errno and returns 0.
 */
static int token_is_number(const char *text, size_t length, Value **result) {
    char *end;
    char *copy = forth_alloc(length + 1);
    int is_float;

    memcpy(copy, text, length);
    copy[length] = '\0';
    errno = 0;
    (void)strtod(copy, &end);
    if (end == copy || *end != '\0' || errno == ERANGE) {
        free(copy);
        return 0;
    }
    is_float = strchr(copy, '.') != NULL || strchr(copy, 'e') != NULL || strchr(copy, 'E') != NULL;
    if (is_float) {
        *result = value_new_float(strtod(copy, NULL));
    } else {
        errno = 0;
        long integer = strtol(copy, &end, 10);
        if (*end != '\0' || errno == ERANGE) {
            free(copy);
            return 0;
        }
        *result = value_new_int(integer);
    }
    free(copy);
    return 1;
}

/*
 * Parse a `" ... "` or `s" ... "` string literal starting at the opening
 * quote. Handles escape sequences (`\n`, `\r`, `\t`, `\\`, `\"`). The
 * closing quote must be present; otherwise the string is unterminated.
 * The caller owns the returned Value*; the allocated buffer is freed.
 */
static Value *parse_string(Parser *parser) {
    Value *text;
    char *buffer;
    size_t length = 0;
    size_t capacity = 32;

    parser->cursor++; /* opening quote */
    if (parser->trim_forth_string_space && *parser->cursor == ' ')
        parser->cursor++;
    parser->trim_forth_string_space = 0;
    buffer = forth_alloc(capacity);
    while (*parser->cursor != '\0' && *parser->cursor != '"') {
        int character = (unsigned char)*parser->cursor++;
        if (character == '\\') {
            character = (unsigned char)*parser->cursor++;
            if (character == '\0') {
                free(buffer);
                parser_error(parser, "unterminated string escape");
                return NULL;
            }
            switch (character) {
            case 'n': character = '\n'; break;
            case 'r': character = '\r'; break;
            case 't': character = '\t'; break;
            case '\\': case '"': break;
            default:
                free(buffer);
                parser_error(parser, "unknown string escape");
                return NULL;
            }
        }
        if (length + 1 == capacity) {
            capacity *= 2;
            buffer = forth_realloc(buffer, capacity);
        }
        buffer[length++] = (char)character;
    }
    if (*parser->cursor != '"') {
        free(buffer);
        parser_error(parser, "unterminated string");
        return NULL;
    }
    parser->cursor++;
    text = value_new_string(buffer, length);
    free(buffer);
    return text;
}

/*
 * Return the next token from the parser cursor: a number, `true`/`false`,
 * or a lowercased symbol. The cursor stops at whitespace, `"`, `(`, `\`.
 * The caller (forth_compile) handles the token based on its type.
 */
static Value *parse_token(Parser *parser) {
    const char *start = parser->cursor;
    Value *number;
    size_t length;

    while (*parser->cursor != '\0' && !isspace((unsigned char)*parser->cursor) &&
           *parser->cursor != '(' && *parser->cursor != '\\')
        parser->cursor++;
    length = (size_t)(parser->cursor - start);
    if (length == 0) {
        parser_error(parser, "unexpected character");
        return NULL;
    }
    if (length == 4 && memcmp(start, "true", 4) == 0)
        return value_new_bool(1);
    if (length == 5 && memcmp(start, "false", 5) == 0)
        return value_new_bool(0);
    if (token_is_number(start, length, &number))
        return number;
    return value_new_symbol(start, length);
}

/*
 * Compile `source` into a program list (owned Value* linked list) suitable
 * for `forth_execute`. Returns NULL on syntax error; surviving stack values
 * are left visible for inspection. Each token's ownership is transferred to
 * the program list via `value_list_append`.
 */
Value *forth_compile(const char *source) {
    Parser parser;
    Value *program = value_new_list();

    parser.source = source;
    parser.cursor = source;
    parser.trim_forth_string_space = 0;
    while (*parser.cursor != '\0') {
        Value *value;

        if (parser_skip_layout(&parser) != 0)
            goto fail;
        if (*parser.cursor == '\0')
            break;
        if ((parser.cursor[0] == '.' || parser.cursor[0] == 's') && parser.cursor[1] == '"') {
            char name[] = { parser.cursor[0], '"', '\0' };
            parser.cursor++;
            parser.trim_forth_string_space = 1;
            value = value_new_symbol(name, 2);
        } else {
            value = *parser.cursor == '"' ? parse_string(&parser) : parse_token(&parser);
        }
        if (value == NULL)
            goto fail;
        value_list_append(program, value);
    }
    return program;

fail:
    value_release(program);
    return NULL;
}
