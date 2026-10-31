#include "forth.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void run(Context *ctx, const char *source) {
    Value *program = forth_compile(source);
    assert(program != NULL);
    assert(forth_execute(ctx, program) == 0);
    value_release(program);
}

static long pop_integer(Context *ctx) {
    Value *value = forth_stack_pop(ctx);
    long number;
    assert(value != NULL && value->type == VALUE_INT);
    number = value->as.integer;
    value_release(value);
    return number;
}

static void run_fails(Context *ctx, const char *source) {
    Value *program = forth_compile(source);
    assert(program != NULL);
    assert(forth_execute(ctx, program) != 0);
    value_release(program);
}

int main(void) {
    Context *ctx = forth_context_new();
    forth_install_core_words(ctx);

    run(ctx, "2 3 + 4 *");
    assert(pop_integer(ctx) == 20);
    run(ctx, "5 2 /");
    assert(pop_integer(ctx) == 2);
    run(ctx, "true if 7 else 99 then");
    assert(pop_integer(ctx) == 7);
    run(ctx, "true if false if 1 else 2 then else 3 then");
    assert(pop_integer(ctx) == 2);
    run(ctx, ": square dup * ; 9 square");
    assert(pop_integer(ctx) == 81);
    run(ctx, "3 begin dup 0 > while 1 - repeat");
    assert(pop_integer(ctx) == 0);
    run(ctx, "1 2 3 rot");
    assert(pop_integer(ctx) == 1);
    assert(pop_integer(ctx) == 3);
    assert(pop_integer(ctx) == 2);
    run(ctx, "1 2 2dup");
    assert(pop_integer(ctx) == 2);
    assert(pop_integer(ctx) == 1);
    assert(pop_integer(ctx) == 2);
    assert(pop_integer(ctx) == 1);
    run(ctx, "1 2 3 2 pick");
    assert(pop_integer(ctx) == 1);
    assert(pop_integer(ctx) == 3);
    assert(pop_integer(ctx) == 2);
    assert(pop_integer(ctx) == 1);
    run(ctx, "1 >r 2 r> +");
    assert(pop_integer(ctx) == 3);
    run(ctx, "42 CONSTANT Answer ANSWER");
    assert(pop_integer(ctx) == 42);
    run(ctx, "variable counter 41 counter ! 1 counter +! counter @");
    assert(pop_integer(ctx) == 42);
    run(ctx, "10 value scale scale 3 + to scale scale");
    assert(pop_integer(ctx) == 13);
    run(ctx, "17 5 /mod");
    assert(pop_integer(ctx) == 3);
    assert(pop_integer(ctx) == 2);
    run(ctx, "5 0 ?do i loop");
    assert(pop_integer(ctx) == 4);
    assert(pop_integer(ctx) == 3);
    assert(pop_integer(ctx) == 2);
    assert(pop_integer(ctx) == 1);
    assert(pop_integer(ctx) == 0);
    run(ctx, "10 0 ?do i dup 3 = if leave then loop");
    assert(pop_integer(ctx) == 3);
    assert(pop_integer(ctx) == 2);
    assert(pop_integer(ctx) == 1);
    assert(pop_integer(ctx) == 0);
    run(ctx, "\\ line comment\n( nested definitions remain ordinary tokens )\n5 2 %");
    assert(pop_integer(ctx) == 1);
    run(ctx, "\"first\\nsecond\"");
    {
        Value *text = forth_stack_pop(ctx);
        assert(text != NULL && text->type == VALUE_STRING);
        assert(strcmp(text->as.string.data, "first\nsecond") == 0);
        value_release(text);
    }
    run(ctx, "s\" compact string\"");
    {
        Value *text = forth_stack_pop(ctx);
        assert(text != NULL && text->type == VALUE_STRING);
        assert(strcmp(text->as.string.data, "compact string") == 0);
        value_release(text);
    }
    run_fails(ctx, "1 0 /");
    assert(pop_integer(ctx) == 0);
    assert(pop_integer(ctx) == 1);

    forth_context_free(ctx);
    puts("all tests passed");
    return 0;
}
