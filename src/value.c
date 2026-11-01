#include "forth.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Allocate `size` bytes, aborting on failure. The VM cannot tolerate
 * a NULL return mid-execution, so out-of-memory is a fatal error.
 */
void *forth_alloc(size_t size) {
    void *pointer = malloc(size == 0 ? 1 : size);
    if (pointer == NULL) {
        fprintf(stderr, "forth: out of memory while allocating %zu bytes\n", size);
        exit(EXIT_FAILURE);
    }
    return pointer;
}

/*
 * Reallocate `pointer` to `size` bytes, aborting on failure. Same fatal
 * contract as `forth_alloc`: a NULL return would corrupt the VM.
 */
void *forth_realloc(void *pointer, size_t size) {
    void *replacement = realloc(pointer, size == 0 ? 1 : size);
    if (replacement == NULL) {
        fprintf(stderr, "forth: out of memory while allocating %zu bytes\n", size);
        exit(EXIT_FAILURE);
    }
    return replacement;
}

char *forth_strdup(const char *text) {
    size_t length = strlen(text) + 1;
    char *copy = forth_alloc(length);
    memcpy(copy, text, length);
    return copy;
}

static Value *value_new(ValueType type) {
    Value *value = forth_alloc(sizeof(*value));
    value->refs = 1;
    value->type = type;
    return value;
}

Value *value_new_int(long number) {
    Value *value = value_new(VALUE_INT);
    value->as.integer = number;
    return value;
}

Value *value_new_float(double number) {
    Value *value = value_new(VALUE_FLOAT);
    value->as.real = number;
    return value;
}

Value *value_new_bool(int truth) {
    Value *value = value_new(VALUE_BOOL);
    value->as.boolean = truth != 0;
    return value;
}

/*
 * Create a string/symbol value owning a NUL-terminated copy of `text`.
 * `length` bytes are copied; the caller retains the original.
 */
static Value *value_new_text(ValueType type, const char *text, size_t length) {
    Value *value = value_new(type);
    value->as.string.data = forth_alloc(length + 1);
    memcpy(value->as.string.data, text, length);
    value->as.string.data[length] = '\0';
    value->as.string.length = length;
    return value;
}

Value *value_new_string(const char *text, size_t length) {
    return value_new_text(VALUE_STRING, text, length);
}

Value *value_new_symbol(const char *text, size_t length) {
    Value *symbol = value_new_text(VALUE_SYMBOL, text, length);
    size_t index;

    /* Forth source is case-insensitive; keeping the canonical form simplifies lookup. */
    for (index = 0; index < length; index++)
        symbol->as.string.data[index] = (char)tolower((unsigned char)symbol->as.string.data[index]);
    return symbol;
}

Value *value_new_list(void) {
    Value *value = value_new(VALUE_LIST);
    value->as.list.items = NULL;
    value->as.list.length = 0;
    value->as.list.capacity = 0;
    return value;
}

Value *value_new_cell(Value *initial) {
    Value *value = value_new(VALUE_CELL);
    value->as.cell.value = initial;
    return value;
}

void value_retain(Value *value) {
    if (value != NULL)
        value->refs++;
}

void value_release(Value *value) {
    size_t index;

    if (value == NULL)
        return;
    if (value->refs == 0) {
        fprintf(stderr, "forth: internal error: invalid value release\n");
        abort();
    }
    if (--value->refs != 0)
        return;

    if (value->type == VALUE_LIST) {
        for (index = 0; index < value->as.list.length; index++)
            value_release(value->as.list.items[index]);
        free(value->as.list.items);
    } else if (value->type == VALUE_STRING || value->type == VALUE_SYMBOL) {
        free(value->as.string.data);
    } else if (value->type == VALUE_CELL) {
        value_release(value->as.cell.value);
    }
    free(value);
}

void value_list_append(Value *list, Value *value) {
    size_t capacity;

    if (list == NULL || list->type != VALUE_LIST || value == NULL) {
        fprintf(stderr, "forth: internal error: invalid list append\n");
        abort();
    }
    if (list->as.list.length == list->as.list.capacity) {
        capacity = list->as.list.capacity == 0 ? 8 : list->as.list.capacity * 2;
        list->as.list.items = forth_realloc(list->as.list.items,
                                            capacity * sizeof(*list->as.list.items));
        list->as.list.capacity = capacity;
    }
    list->as.list.items[list->as.list.length++] = value;
}

const char *forth_type_name(ValueType type) {
    switch (type) {
    case VALUE_INT: return "integer";
    case VALUE_FLOAT: return "float";
    case VALUE_BOOL: return "boolean";
    case VALUE_STRING: return "string";
    case VALUE_SYMBOL: return "symbol";
    case VALUE_LIST: return "list";
    case VALUE_CELL: return "address";
    }
    return "unknown";
}

void forth_print_value(const Value *value) {
    size_t index;

    if (value == NULL) {
        fputs("<null>", stdout);
        return;
    }
    switch (value->type) {
    case VALUE_INT: printf("%ld", value->as.integer); break;
    case VALUE_FLOAT: printf("%.12g", value->as.real); break;
    case VALUE_BOOL: fputs(value->as.boolean ? "true" : "false", stdout); break;
    case VALUE_STRING: printf("\"%s\"", value->as.string.data); break;
    case VALUE_SYMBOL: fputs(value->as.string.data, stdout); break;
    case VALUE_LIST:
        putchar('[');
        for (index = 0; index < value->as.list.length; index++) {
            if (index != 0) putchar(' ');
            forth_print_value(value->as.list.items[index]);
        }
        putchar(']');
        break;
    case VALUE_CELL:
        fputs("<addr>", stdout);
        break;
    }
}
