#include "forth.h"

#include <stdlib.h>
#include <string.h>

/*
 * Allocate and zero-initialize a fresh execution context: data stack,
 * return stack, dictionary, and loop-index stack. The caller owns the
 * returned pointer and must release it with `forth_context_free`.
 */
Context *forth_context_new(void) {
    Context *ctx = forth_alloc(sizeof(*ctx));
    ctx->stack = value_new_list();
    ctx->return_stack = value_new_list();
    ctx->dictionary.entries = NULL;
    ctx->dictionary.length = 0;
    ctx->dictionary.capacity = 0;
    ctx->loop_indices = NULL;
    ctx->loop_depth = 0;
    ctx->loop_capacity = 0;
    ctx->execution_depth = 0;
    return ctx;
}

/*
 * Deep-free a context: release the stacks, dictionary words, and loop
 * indices. This cleans up all owned references and memory.
 */
void forth_context_free(Context *ctx) {
    size_t index;

    if (ctx == NULL)
        return;
    value_release(ctx->stack);
    value_release(ctx->return_stack);
    for (index = 0; index < ctx->dictionary.length; index++) {
        free(ctx->dictionary.entries[index].name);
        value_release(ctx->dictionary.entries[index].body);
        value_release(ctx->dictionary.entries[index].data);
    }
    free(ctx->dictionary.entries);
    free(ctx->loop_indices);
    free(ctx);
}

void forth_stack_push(Context *ctx, Value *value) {
    value_list_append(ctx->stack, value);
}

Value *forth_stack_pop(Context *ctx) {
    Value *value;

    if (ctx->stack->as.list.length == 0)
        return NULL;
    value = ctx->stack->as.list.items[--ctx->stack->as.list.length];
    return value;
}

Value *forth_stack_peek(const Context *ctx) {
    size_t depth = ctx->stack->as.list.length;
    return depth == 0 ? NULL : ctx->stack->as.list.items[depth - 1];
}

size_t forth_stack_depth(const Context *ctx) {
    return ctx->stack->as.list.length;
}

int forth_dictionary_add(Context *ctx, const char *name, BuiltinFn builtin, Value *body) {
    Dictionary *dictionary = &ctx->dictionary;
    Word *word;
    size_t capacity;

    if (dictionary->length == dictionary->capacity) {
        capacity = dictionary->capacity == 0 ? 16 : dictionary->capacity * 2;
        dictionary->entries = forth_realloc(dictionary->entries,
                                            capacity * sizeof(*dictionary->entries));
        dictionary->capacity = capacity;
    }
    word = &dictionary->entries[dictionary->length++];
    word->name = forth_strdup(name);
    word->type = builtin != NULL ? WORD_BUILTIN : WORD_COLON;
    word->builtin = builtin;
    word->body = body;
    word->data = NULL;
    return 0;
}

int forth_dictionary_add_data(Context *ctx, const char *name, WordType type, Value *data) {
    Dictionary *dictionary = &ctx->dictionary;
    Word *word;
    size_t capacity;

    if (dictionary->length == dictionary->capacity) {
        capacity = dictionary->capacity == 0 ? 16 : dictionary->capacity * 2;
        dictionary->entries = forth_realloc(dictionary->entries,
                                            capacity * sizeof(*dictionary->entries));
        dictionary->capacity = capacity;
    }
    word = &dictionary->entries[dictionary->length++];
    word->name = forth_strdup(name);
    word->type = type;
    word->builtin = NULL;
    word->body = NULL;
    word->data = data;
    return 0;
}

Word *forth_dictionary_find(Context *ctx, const char *name) {
    size_t index = ctx->dictionary.length;

    /* Searching backward makes redefinition cheap and gives newer words precedence. */
    while (index != 0) {
        Word *word = &ctx->dictionary.entries[--index];
        if (strcmp(word->name, name) == 0)
            return word;
    }
    return NULL;
}
