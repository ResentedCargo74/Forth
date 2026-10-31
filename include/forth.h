#ifndef FORTH_H
#define FORTH_H

#include <stddef.h>

typedef enum {
    VALUE_INT,
    VALUE_FLOAT,
    VALUE_BOOL,
    VALUE_STRING,
    VALUE_SYMBOL,
    VALUE_LIST,
    VALUE_CELL
} ValueType;

typedef struct Value Value;
typedef struct Context Context;
typedef int (*BuiltinFn)(Context *ctx, const char *name);
typedef enum {
    WORD_BUILTIN,
    WORD_COLON,
    WORD_CONSTANT,
    WORD_VARIABLE,
    WORD_VALUE
} WordType;

struct Value {
    unsigned int refs;
    ValueType type;
    union {
        long integer;
        double real;
        int boolean;
        struct { char *data; size_t length; } string;
        struct { Value **items; size_t length; size_t capacity; } list;
        struct { Value *value; } cell;
    } as;
};

typedef struct {
    char *name;
    WordType type;
    BuiltinFn builtin;
    Value *body;
    Value *data;
} Word;

typedef struct {
    Word *entries;
    size_t length;
    size_t capacity;
} Dictionary;

struct Context {
    Value *stack;
    Value *return_stack;
    Dictionary dictionary;
    long *loop_indices;
    size_t loop_depth;
    size_t loop_capacity;
    unsigned int execution_depth;
};

/* Allocation deliberately aborts: continuing after OOM corrupts a stack VM. */
void *forth_alloc(size_t size);
void *forth_realloc(void *pointer, size_t size);
char *forth_strdup(const char *text);

Value *value_new_int(long number);
Value *value_new_float(double number);
Value *value_new_bool(int truth);
Value *value_new_string(const char *text, size_t length);
Value *value_new_symbol(const char *text, size_t length);
Value *value_new_list(void);
Value *value_new_cell(Value *initial); /* Takes ownership of initial. */
void value_retain(Value *value);
void value_release(Value *value);
void value_list_append(Value *list, Value *value); /* Takes ownership of one reference. */

Context *forth_context_new(void);
void forth_context_free(Context *ctx);
void forth_stack_push(Context *ctx, Value *value); /* Takes ownership of one reference. */
Value *forth_stack_pop(Context *ctx);
Value *forth_stack_peek(const Context *ctx);
size_t forth_stack_depth(const Context *ctx);

int forth_dictionary_add(Context *ctx, const char *name, BuiltinFn builtin, Value *body);
int forth_dictionary_add_data(Context *ctx, const char *name, WordType type, Value *data);
Word *forth_dictionary_find(Context *ctx, const char *name);

Value *forth_compile(const char *source);
int forth_execute(Context *ctx, Value *program);
void forth_install_core_words(Context *ctx);
int forth_repl(Context *ctx);
void forth_print_value(const Value *value);
const char *forth_type_name(ValueType type);

#endif
