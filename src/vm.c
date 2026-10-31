#include "forth.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

#define FORTH_MAX_EXECUTION_DEPTH 1024U
#define EXEC_LEAVE 1

static int execute_range(Context *ctx, Value *program, size_t first, size_t last);

static int is_symbol(const Value *value, const char *name) {
    return value->type == VALUE_SYMBOL && strcmp(value->as.string.data, name) == 0;
}

static int is_number(const Value *value) {
    return value->type == VALUE_INT || value->type == VALUE_FLOAT;
}

static double as_double(const Value *value) {
    return value->type == VALUE_INT ? (double)value->as.integer : value->as.real;
}

static int as_truth(const Value *value, int *truth) {
    if (value->type == VALUE_BOOL) {
        *truth = value->as.boolean;
        return 0;
    }
    if (value->type == VALUE_INT) {
        *truth = value->as.integer != 0;
        return 0;
    }
    return -1;
}

static void stack_error(const char *word, const char *message) {
    fprintf(stderr, "forth: %s: %s\n", word, message);
}

static int pop_pair(Context *ctx, const char *word, Value **left, Value **right) {
    *right = forth_stack_pop(ctx);
    if (*right == NULL) {
        stack_error(word, "stack underflow");
        return -1;
    }
    *left = forth_stack_pop(ctx);
    if (*left == NULL) {
        stack_error(word, "stack underflow");
        forth_stack_push(ctx, *right);
        return -1;
    }
    return 0;
}

static int builtin_math(Context *ctx, const char *word) {
    Value *left;
    Value *right;
    Value *result;
    double a, b;

    if (pop_pair(ctx, word, &left, &right) != 0)
        return -1;
    if (!is_number(left) || !is_number(right)) {
        stack_error(word, "expects two numbers");
        forth_stack_push(ctx, left);
        forth_stack_push(ctx, right);
        return -1;
    }

    if (strcmp(word, "%") == 0 || strcmp(word, "mod") == 0) {
        if (left->type != VALUE_INT || right->type != VALUE_INT) {
            stack_error(word, "expects two integers");
            forth_stack_push(ctx, left);
            forth_stack_push(ctx, right);
            return -1;
        }
        if (right->as.integer == 0) {
            stack_error(word, "division by zero");
            forth_stack_push(ctx, left);
            forth_stack_push(ctx, right);
            return -1;
        }
        if (left->as.integer == LONG_MIN && right->as.integer == -1) {
            /* C leaves this quotient unrepresentable, even though the remainder is zero. */
            stack_error(word, "integer overflow");
            forth_stack_push(ctx, left);
            forth_stack_push(ctx, right);
            return -1;
        }
        result = value_new_int(left->as.integer % right->as.integer);
    } else if (left->type == VALUE_INT && right->type == VALUE_INT) {
        long a_int = left->as.integer;
        long b_int = right->as.integer;
        if (strcmp(word, "/") == 0) {
            if (b_int == 0) {
                stack_error(word, "division by zero");
                forth_stack_push(ctx, left);
                forth_stack_push(ctx, right);
                return -1;
            }
            if (a_int == LONG_MIN && b_int == -1) {
                stack_error(word, "integer overflow");
                forth_stack_push(ctx, left);
                forth_stack_push(ctx, right);
                return -1;
            }
            result = value_new_int(a_int / b_int);
        } else if ((strcmp(word, "+") == 0 && ((b_int > 0 && a_int > LONG_MAX - b_int) ||
                                         (b_int < 0 && a_int < LONG_MIN - b_int))) ||
            (strcmp(word, "-") == 0 && ((b_int < 0 && a_int > LONG_MAX + b_int) ||
                                         (b_int > 0 && a_int < LONG_MIN + b_int))) ||
            (strcmp(word, "*") == 0 && a_int != 0 &&
             ((a_int == -1 && b_int == LONG_MIN) || (b_int == -1 && a_int == LONG_MIN) ||
              (a_int > 0 ? (b_int > 0 ? a_int > LONG_MAX / b_int : b_int < LONG_MIN / a_int)
                         : (b_int > 0 ? a_int < LONG_MIN / b_int : a_int < LONG_MAX / b_int))))) {
            stack_error(word, "integer overflow");
            forth_stack_push(ctx, left);
            forth_stack_push(ctx, right);
            return -1;
        } else {
            if (strcmp(word, "+") == 0) result = value_new_int(a_int + b_int);
            else if (strcmp(word, "-") == 0) result = value_new_int(a_int - b_int);
            else result = value_new_int(a_int * b_int);
        }
    } else {
        a = as_double(left);
        b = as_double(right);
        if (strcmp(word, "/") == 0 && b == 0.0) {
            stack_error(word, "division by zero");
            forth_stack_push(ctx, left);
            forth_stack_push(ctx, right);
            return -1;
        }
        if (strcmp(word, "+") == 0) result = value_new_float(a + b);
        else if (strcmp(word, "-") == 0) result = value_new_float(a - b);
        else if (strcmp(word, "*") == 0) result = value_new_float(a * b);
        else result = value_new_float(a / b);
    }
    value_release(left);
    value_release(right);
    forth_stack_push(ctx, result);
    return 0;
}

static int builtin_unary_math(Context *ctx, const char *word) {
    Value *value = forth_stack_pop(ctx);
    long number;
    long result;

    if (value == NULL) { stack_error(word, "stack underflow"); return -1; }
    if (value->type != VALUE_INT) {
        stack_error(word, "expects an integer");
        forth_stack_push(ctx, value);
        return -1;
    }
    number = value->as.integer;
    if ((strcmp(word, "negate") == 0 || strcmp(word, "abs") == 0) && number == LONG_MIN) {
        stack_error(word, "integer overflow");
        forth_stack_push(ctx, value);
        return -1;
    }
    if (strcmp(word, "1+") == 0 && number == LONG_MAX) {
        stack_error(word, "integer overflow");
        forth_stack_push(ctx, value);
        return -1;
    }
    if (strcmp(word, "1-") == 0 && number == LONG_MIN) {
        stack_error(word, "integer overflow");
        forth_stack_push(ctx, value);
        return -1;
    }
    if (strcmp(word, "2*") == 0 && (number > LONG_MAX / 2 || number < LONG_MIN / 2)) {
        stack_error(word, "integer overflow");
        forth_stack_push(ctx, value);
        return -1;
    }
    if (strcmp(word, "negate") == 0) result = -number;
    else if (strcmp(word, "abs") == 0) result = number < 0 ? -number : number;
    else if (strcmp(word, "1+") == 0) result = number + 1;
    else if (strcmp(word, "1-") == 0) result = number - 1;
    else if (strcmp(word, "2*") == 0) result = number * 2;
    else result = number / 2;
    value_release(value);
    forth_stack_push(ctx, value_new_int(result));
    return 0;
}

static int builtin_min_max(Context *ctx, const char *word) {
    Value *left;
    Value *right;
    Value *result;
    if (pop_pair(ctx, word, &left, &right) != 0) return -1;
    if (!is_number(left) || !is_number(right)) {
        stack_error(word, "expects two numbers");
        forth_stack_push(ctx, left);
        forth_stack_push(ctx, right);
        return -1;
    }
    if (left->type == VALUE_INT && right->type == VALUE_INT) {
        long selected = strcmp(word, "min") == 0 ?
                        (left->as.integer < right->as.integer ? left->as.integer : right->as.integer) :
                        (left->as.integer > right->as.integer ? left->as.integer : right->as.integer);
        result = value_new_int(selected);
    } else {
        double selected = strcmp(word, "min") == 0 ?
                          (as_double(left) < as_double(right) ? as_double(left) : as_double(right)) :
                          (as_double(left) > as_double(right) ? as_double(left) : as_double(right));
        result = value_new_float(selected);
    }
    value_release(left);
    value_release(right);
    forth_stack_push(ctx, result);
    return 0;
}

static int builtin_compare(Context *ctx, const char *word) {
    Value *left;
    Value *right;
    int comparison;
    int answer;

    if (pop_pair(ctx, word, &left, &right) != 0)
        return -1;
    if (is_number(left) && is_number(right)) {
        double a = as_double(left);
        double b = as_double(right);
        comparison = a == b ? 0 : (a < b ? -1 : 1);
    } else if ((left->type == VALUE_STRING || left->type == VALUE_SYMBOL) &&
               (right->type == VALUE_STRING || right->type == VALUE_SYMBOL)) {
        comparison = strcmp(left->as.string.data, right->as.string.data);
    } else if (left->type == VALUE_BOOL && right->type == VALUE_BOOL) {
        comparison = left->as.boolean - right->as.boolean;
    } else {
        stack_error(word, "expects comparable values");
        forth_stack_push(ctx, left);
        forth_stack_push(ctx, right);
        return -1;
    }
    answer = strcmp(word, "=") == 0 ? comparison == 0 :
             strcmp(word, "<>") == 0 ? comparison != 0 :
             strcmp(word, "<") == 0 ? comparison < 0 :
             strcmp(word, ">") == 0 ? comparison > 0 :
             strcmp(word, "<=") == 0 ? comparison <= 0 : comparison >= 0;
    value_release(left);
    value_release(right);
    forth_stack_push(ctx, value_new_bool(answer));
    return 0;
}

static int builtin_dot(Context *ctx, const char *word) {
    Value *value = forth_stack_pop(ctx);
    (void)word;
    if (value == NULL) {
        stack_error(".", "stack underflow");
        return -1;
    }
    forth_print_value(value);
    putchar(' ');
    value_release(value);
    return 0;
}

static int builtin_dots(Context *ctx, const char *word) {
    (void)word;
    forth_print_value(ctx->stack);
    putchar('\n');
    return 0;
}

static int builtin_dup(Context *ctx, const char *word) {
    Value *value = forth_stack_peek(ctx);
    (void)word;
    if (value == NULL) { stack_error("dup", "stack underflow"); return -1; }
    value_retain(value);
    forth_stack_push(ctx, value);
    return 0;
}

static int builtin_drop(Context *ctx, const char *word) {
    Value *value = forth_stack_pop(ctx);
    (void)word;
    if (value == NULL) { stack_error("drop", "stack underflow"); return -1; }
    value_release(value);
    return 0;
}

static int builtin_swap(Context *ctx, const char *word) {
    Value *left;
    Value *right;
    if (pop_pair(ctx, word, &left, &right) != 0) return -1;
    forth_stack_push(ctx, right);
    forth_stack_push(ctx, left);
    return 0;
}

static int builtin_over(Context *ctx, const char *word) {
    size_t depth = forth_stack_depth(ctx);
    Value *value;
    (void)word;
    if (depth < 2) { stack_error("over", "stack underflow"); return -1; }
    value = ctx->stack->as.list.items[depth - 2];
    value_retain(value);
    forth_stack_push(ctx, value);
    return 0;
}

static int builtin_rot(Context *ctx, const char *word) {
    Value *a;
    Value *b;
    Value *c;
    (void)word;
    if (forth_stack_depth(ctx) < 3) { stack_error("rot", "stack underflow"); return -1; }
    c = forth_stack_pop(ctx);
    b = forth_stack_pop(ctx);
    a = forth_stack_pop(ctx);
    forth_stack_push(ctx, b);
    forth_stack_push(ctx, c);
    forth_stack_push(ctx, a);
    return 0;
}

static int builtin_qdup(Context *ctx, const char *word) {
    Value *value = forth_stack_peek(ctx);
    int truth;
    (void)word;
    if (value == NULL) { stack_error("?dup", "stack underflow"); return -1; }
    if (as_truth(value, &truth) != 0) {
        stack_error("?dup", "expects a boolean or integer");
        return -1;
    }
    if (truth) {
        value_retain(value);
        forth_stack_push(ctx, value);
    }
    return 0;
}

static int builtin_two_stack(Context *ctx, const char *word) {
    size_t depth = forth_stack_depth(ctx);
    Value **items = ctx->stack->as.list.items;

    if (strcmp(word, "2dup") == 0 || strcmp(word, "2over") == 0) {
        size_t offset = strcmp(word, "2dup") == 0 ? 2 : 4;
        Value *first;
        Value *second;
        if (depth < offset) { stack_error(word, "stack underflow"); return -1; }
        first = items[depth - offset];
        second = items[depth - offset + 1];
        value_retain(first);
        value_retain(second);
        forth_stack_push(ctx, first);
        forth_stack_push(ctx, second);
        return 0;
    }
    if (strcmp(word, "2drop") == 0) {
        Value *first;
        Value *second;
        if (depth < 2) { stack_error(word, "stack underflow"); return -1; }
        second = forth_stack_pop(ctx);
        first = forth_stack_pop(ctx);
        value_release(first);
        value_release(second);
        return 0;
    }
    if (depth < 4) { stack_error(word, "stack underflow"); return -1; }
    {
        Value *a = items[depth - 4];
        Value *b = items[depth - 3];
        items[depth - 4] = items[depth - 2];
        items[depth - 3] = items[depth - 1];
        items[depth - 2] = a;
        items[depth - 1] = b;
    }
    return 0;
}

static int builtin_nip(Context *ctx, const char *word) {
    Value *left;
    Value *right;
    if (pop_pair(ctx, word, &left, &right) != 0) return -1;
    value_release(left);
    forth_stack_push(ctx, right);
    return 0;
}

static int builtin_tuck(Context *ctx, const char *word) {
    Value *left;
    Value *right;
    if (pop_pair(ctx, word, &left, &right) != 0) return -1;
    value_retain(right);
    forth_stack_push(ctx, right);
    forth_stack_push(ctx, left);
    forth_stack_push(ctx, right);
    return 0;
}

static int builtin_minus_rot(Context *ctx, const char *word) {
    Value *a;
    Value *b;
    Value *c;
    (void)word;
    if (forth_stack_depth(ctx) < 3) { stack_error("-rot", "stack underflow"); return -1; }
    c = forth_stack_pop(ctx);
    b = forth_stack_pop(ctx);
    a = forth_stack_pop(ctx);
    forth_stack_push(ctx, c);
    forth_stack_push(ctx, a);
    forth_stack_push(ctx, b);
    return 0;
}

static int builtin_pick_roll(Context *ctx, const char *word) {
    Value *index = forth_stack_pop(ctx);
    size_t depth;
    size_t position;

    if (index == NULL) { stack_error(word, "stack underflow"); return -1; }
    if (index->type != VALUE_INT || index->as.integer < 0) {
        stack_error(word, "expects a non-negative integer");
        forth_stack_push(ctx, index);
        return -1;
    }
    depth = forth_stack_depth(ctx);
    if ((unsigned long)index->as.integer >= depth) {
        stack_error(word, "stack underflow");
        forth_stack_push(ctx, index);
        return -1;
    }
    position = depth - 1 - (size_t)index->as.integer;
    value_release(index);
    if (strcmp(word, "pick") == 0) {
        Value *value = ctx->stack->as.list.items[position];
        value_retain(value);
        forth_stack_push(ctx, value);
    } else {
        Value *value = ctx->stack->as.list.items[position];
        memmove(&ctx->stack->as.list.items[position], &ctx->stack->as.list.items[position + 1],
                (depth - position - 1) * sizeof(*ctx->stack->as.list.items));
        ctx->stack->as.list.items[depth - 1] = value;
    }
    return 0;
}

static int builtin_clear(Context *ctx, const char *word) {
    Value *value;
    (void)word;
    while ((value = forth_stack_pop(ctx)) != NULL)
        value_release(value);
    return 0;
}

static int builtin_depth(Context *ctx, const char *word) {
    (void)word;
    forth_stack_push(ctx, value_new_int((long)forth_stack_depth(ctx)));
    return 0;
}

static Value *pop_return(Context *ctx) {
    Value *stack = ctx->return_stack;
    return stack->as.list.length == 0 ? NULL : stack->as.list.items[--stack->as.list.length];
}

static int builtin_return_stack(Context *ctx, const char *word) {
    Value *value;

    if (strcmp(word, ">r") == 0) {
        value = forth_stack_pop(ctx);
        if (value == NULL) { stack_error(word, "stack underflow"); return -1; }
        value_list_append(ctx->return_stack, value);
        return 0;
    }
    if (strcmp(word, "r>") == 0) {
        value = pop_return(ctx);
        if (value == NULL) { stack_error(word, "return stack underflow"); return -1; }
        forth_stack_push(ctx, value);
        return 0;
    }
    value = ctx->return_stack->as.list.length == 0 ? NULL :
            ctx->return_stack->as.list.items[ctx->return_stack->as.list.length - 1];
    if (value == NULL) { stack_error(word, "return stack underflow"); return -1; }
    value_retain(value);
    forth_stack_push(ctx, value);
    return 0;
}

static int builtin_fetch(Context *ctx, const char *word) {
    Value *address = forth_stack_pop(ctx);
    Value *value;
    (void)word;
    if (address == NULL) { stack_error("@", "stack underflow"); return -1; }
    if (address->type != VALUE_CELL) {
        stack_error("@", "expects an address");
        forth_stack_push(ctx, address);
        return -1;
    }
    value = address->as.cell.value;
    value_retain(value);
    value_release(address);
    forth_stack_push(ctx, value);
    return 0;
}

static int builtin_store(Context *ctx, const char *word) {
    Value *value;
    Value *address;
    Value *old;
    (void)word;
    if (pop_pair(ctx, "!", &value, &address) != 0) return -1;
    if (address->type != VALUE_CELL) {
        stack_error("!", "expects an address");
        forth_stack_push(ctx, value);
        forth_stack_push(ctx, address);
        return -1;
    }
    old = address->as.cell.value;
    address->as.cell.value = value;
    value_release(old);
    value_release(address);
    return 0;
}

static int builtin_add_store(Context *ctx, const char *word) {
    Value *increment;
    Value *address;
    long current;
    long delta;
    (void)word;
    if (pop_pair(ctx, "+!", &increment, &address) != 0) return -1;
    if (increment->type != VALUE_INT || address->type != VALUE_CELL ||
        address->as.cell.value->type != VALUE_INT) {
        stack_error("+!", "expects an integer and an integer address");
        forth_stack_push(ctx, increment);
        forth_stack_push(ctx, address);
        return -1;
    }
    current = address->as.cell.value->as.integer;
    delta = increment->as.integer;
    if ((delta > 0 && current > LONG_MAX - delta) || (delta < 0 && current < LONG_MIN - delta)) {
        stack_error("+!", "integer overflow");
        forth_stack_push(ctx, increment);
        forth_stack_push(ctx, address);
        return -1;
    }
    value_release(address->as.cell.value);
    address->as.cell.value = value_new_int(current + delta);
    value_release(increment);
    value_release(address);
    return 0;
}

static int builtin_question(Context *ctx, const char *word) {
    (void)word;
    if (builtin_fetch(ctx, "@") != 0) return -1;
    return builtin_dot(ctx, ".");
}

static int builtin_loop_index(Context *ctx, const char *word) {
    size_t needed = strcmp(word, "i") == 0 ? 1 : 2;
    if (ctx->loop_depth < needed) {
        stack_error(word, "used outside a counted loop");
        return -1;
    }
    forth_stack_push(ctx, value_new_int(ctx->loop_indices[ctx->loop_depth - needed]));
    return 0;
}

static int builtin_boolean(Context *ctx, const char *word) {
    Value *left;
    Value *right;
    int a, b;
    if (strcmp(word, "not") == 0) {
        left = forth_stack_pop(ctx);
        if (left == NULL) { stack_error(word, "stack underflow"); return -1; }
        if (as_truth(left, &a) != 0) {
            stack_error(word, "expects a boolean or integer");
            forth_stack_push(ctx, left);
            return -1;
        }
        value_release(left);
        forth_stack_push(ctx, value_new_bool(!a));
        return 0;
    }
    if (pop_pair(ctx, word, &left, &right) != 0) return -1;
    if (as_truth(left, &a) != 0 || as_truth(right, &b) != 0) {
        stack_error(word, "expects boolean or integer operands");
        forth_stack_push(ctx, left);
        forth_stack_push(ctx, right);
        return -1;
    }
    value_release(left);
    value_release(right);
    forth_stack_push(ctx, value_new_bool(strcmp(word, "and") == 0 ? a && b : a || b));
    return 0;
}

static int builtin_zero_compare(Context *ctx, const char *word) {
    Value *value = forth_stack_pop(ctx);
    int answer;

    if (value == NULL) { stack_error(word, "stack underflow"); return -1; }
    if (value->type != VALUE_INT) {
        stack_error(word, "expects an integer");
        forth_stack_push(ctx, value);
        return -1;
    }
    answer = strcmp(word, "0=") == 0 ? value->as.integer == 0 :
             strcmp(word, "0<>") == 0 ? value->as.integer != 0 :
             strcmp(word, "0<") == 0 ? value->as.integer < 0 : value->as.integer > 0;
    value_release(value);
    forth_stack_push(ctx, value_new_bool(answer));
    return 0;
}

static int builtin_bitwise(Context *ctx, const char *word) {
    Value *left;
    Value *right;
    long result;

    if (strcmp(word, "invert") == 0) {
        left = forth_stack_pop(ctx);
        if (left == NULL) { stack_error(word, "stack underflow"); return -1; }
        if (left->type != VALUE_INT) {
            stack_error(word, "expects an integer");
            forth_stack_push(ctx, left);
            return -1;
        }
        result = ~left->as.integer;
        value_release(left);
        forth_stack_push(ctx, value_new_int(result));
        return 0;
    }
    if (pop_pair(ctx, word, &left, &right) != 0) return -1;
    if (left->type != VALUE_INT || right->type != VALUE_INT) {
        stack_error(word, "expects two integers");
        forth_stack_push(ctx, left);
        forth_stack_push(ctx, right);
        return -1;
    }
    if ((strcmp(word, "lshift") == 0 || strcmp(word, "rshift") == 0) &&
        (right->as.integer < 0 || right->as.integer >= (long)(sizeof(long) * CHAR_BIT))) {
        stack_error(word, "invalid shift count");
        forth_stack_push(ctx, left);
        forth_stack_push(ctx, right);
        return -1;
    }
    if (strcmp(word, "and") == 0) result = left->as.integer & right->as.integer;
    else if (strcmp(word, "or") == 0) result = left->as.integer | right->as.integer;
    else if (strcmp(word, "xor") == 0) result = left->as.integer ^ right->as.integer;
    else if (strcmp(word, "lshift") == 0) result = (long)((unsigned long)left->as.integer << right->as.integer);
    else result = (long)((unsigned long)left->as.integer >> right->as.integer);
    value_release(left);
    value_release(right);
    forth_stack_push(ctx, value_new_int(result));
    return 0;
}

static int builtin_divmod(Context *ctx, const char *word) {
    Value *left;
    Value *right;
    long quotient;
    long remainder;
    (void)word;

    if (pop_pair(ctx, "/mod", &left, &right) != 0) return -1;
    if (left->type != VALUE_INT || right->type != VALUE_INT) {
        stack_error("/mod", "expects two integers");
        forth_stack_push(ctx, left);
        forth_stack_push(ctx, right);
        return -1;
    }
    if (right->as.integer == 0 || (left->as.integer == LONG_MIN && right->as.integer == -1)) {
        stack_error("/mod", right->as.integer == 0 ? "division by zero" : "integer overflow");
        forth_stack_push(ctx, left);
        forth_stack_push(ctx, right);
        return -1;
    }
    quotient = left->as.integer / right->as.integer;
    remainder = left->as.integer % right->as.integer;
    value_release(left);
    value_release(right);
    /* The remainder precedes the quotient, as specified for the core word /MOD. */
    forth_stack_push(ctx, value_new_int(remainder));
    forth_stack_push(ctx, value_new_int(quotient));
    return 0;
}

static int builtin_cr(Context *ctx, const char *word) {
    (void)ctx;
    (void)word;
    putchar('\n');
    return 0;
}

static int builtin_emit(Context *ctx, const char *word) {
    Value *value = forth_stack_pop(ctx);
    (void)word;
    if (value == NULL) { stack_error("emit", "stack underflow"); return -1; }
    if (value->type != VALUE_INT || value->as.integer < 0 || value->as.integer > 255) {
        stack_error("emit", "expects an integer byte");
        forth_stack_push(ctx, value);
        return -1;
    }
    putchar((unsigned char)value->as.integer);
    value_release(value);
    return 0;
}

static int builtin_words(Context *ctx, const char *word) {
    size_t index;
    (void)word;
    for (index = 0; index < ctx->dictionary.length; index++)
        printf("%s%c", ctx->dictionary.entries[index].name,
               index + 1 == ctx->dictionary.length ? '\n' : ' ');
    return 0;
}

static int find_definition_end(Value *program, size_t start, size_t last, size_t *end) {
    size_t index;
    for (index = start; index < last; index++) {
        if (is_symbol(program->as.list.items[index], ";")) {
            *end = index;
            return 0;
        }
    }
    return -1;
}

static int find_if_end(Value *program, size_t start, size_t last, size_t *else_at, size_t *then_at) {
    size_t index;
    unsigned int depth = 1;
    *else_at = last;
    for (index = start; index < last; index++) {
        Value *token = program->as.list.items[index];
        if (is_symbol(token, "if")) depth++;
        else if (is_symbol(token, "then") && --depth == 0) {
            *then_at = index;
            return 0;
        } else if (is_symbol(token, "else") && depth == 1) {
            if (*else_at != last) return -1;
            *else_at = index;
        }
    }
    return -1;
}

static int find_loop_end(Value *program, size_t start, size_t last, size_t *while_at, size_t *repeat_at) {
    size_t index;
    unsigned int begin_depth = 1;
    unsigned int if_depth = 0;
    *while_at = last;
    for (index = start; index < last; index++) {
        Value *token = program->as.list.items[index];
        if (is_symbol(token, "begin")) begin_depth++;
        else if (is_symbol(token, "repeat") && --begin_depth == 0) {
            if (*while_at == last) return -1;
            *repeat_at = index;
            return 0;
        } else if (begin_depth == 1 && is_symbol(token, "if")) if_depth++;
        else if (begin_depth == 1 && is_symbol(token, "then") && if_depth != 0) if_depth--;
        else if (begin_depth == 1 && if_depth == 0 && is_symbol(token, "while")) {
            if (*while_at != last) return -1;
            *while_at = index;
        }
    }
    return -1;
}

static int find_counted_loop_end(Value *program, size_t start, size_t last, size_t *end) {
    size_t cursor;
    unsigned int depth = 1;
    for (cursor = start; cursor < last; cursor++) {
        Value *token = program->as.list.items[cursor];
        if (is_symbol(token, "do") || is_symbol(token, "?do")) {
            depth++;
        } else if (is_symbol(token, "loop") || is_symbol(token, "+loop")) {
            if (--depth == 0) {
                *end = cursor;
                return 0;
            }
        }
    }
    return -1;
}

static void loop_push(Context *ctx, long index) {
    size_t capacity;
    if (ctx->loop_depth == ctx->loop_capacity) {
        capacity = ctx->loop_capacity == 0 ? 8 : ctx->loop_capacity * 2;
        ctx->loop_indices = forth_realloc(ctx->loop_indices, capacity * sizeof(*ctx->loop_indices));
        ctx->loop_capacity = capacity;
    }
    ctx->loop_indices[ctx->loop_depth++] = index;
}

static void loop_pop(Context *ctx) {
    if (ctx->loop_depth != 0)
        ctx->loop_depth--;
}

static int advance_loop(long index, long step, long *next) {
    if ((step > 0 && index > LONG_MAX - step) || (step < 0 && index < LONG_MIN - step))
        return -1;
    *next = index + step;
    return 0;
}

static int execute_counted_loop(Context *ctx, Value *program, size_t *index, size_t last, int conditional) {
    Value *limit;
    Value *start;
    size_t end;
    long counter;
    long boundary;

    if (find_counted_loop_end(program, *index + 1, last, &end) != 0) {
        stack_error(conditional ? "?do" : "do", "missing loop terminator");
        return -1;
    }
    if (pop_pair(ctx, conditional ? "?do" : "do", &limit, &start) != 0) return -1;
    if (limit->type != VALUE_INT || start->type != VALUE_INT) {
        stack_error(conditional ? "?do" : "do", "expects limit and start integers");
        forth_stack_push(ctx, limit);
        forth_stack_push(ctx, start);
        return -1;
    }
    boundary = limit->as.integer;
    counter = start->as.integer;
    value_release(limit);
    value_release(start);
    if (conditional && counter == boundary) {
        *index = end;
        return 0;
    }

    loop_push(ctx, counter);
    for (;;) {
        Value *step_value;
        long step = 1;
        long next;
        int result = execute_range(ctx, program, *index + 1, end);
        if (result == EXEC_LEAVE) break;
        if (result != 0) {
            loop_pop(ctx);
            return result;
        }
        if (is_symbol(program->as.list.items[end], "+loop")) {
            step_value = forth_stack_pop(ctx);
            if (step_value == NULL || step_value->type != VALUE_INT) {
                if (step_value != NULL) forth_stack_push(ctx, step_value);
                stack_error("+loop", "expects an integer increment");
                loop_pop(ctx);
                return -1;
            }
            step = step_value->as.integer;
            value_release(step_value);
            if (step == 0) {
                stack_error("+loop", "zero increment would not progress");
                loop_pop(ctx);
                return -1;
            }
        }
        if (advance_loop(ctx->loop_indices[ctx->loop_depth - 1], step, &next) != 0) {
            stack_error(is_symbol(program->as.list.items[end], "+loop") ? "+loop" : "loop",
                        "loop index overflow");
            loop_pop(ctx);
            return -1;
        }
        if (next == boundary || (step > 0 && counter < boundary && next > boundary) ||
            (step < 0 && counter > boundary && next < boundary))
            break;
        ctx->loop_indices[ctx->loop_depth - 1] = next;
        counter = next;
    }
    loop_pop(ctx);
    *index = end;
    return 0;
}

static int execute_if(Context *ctx, Value *program, size_t *index, size_t last) {
    Value *condition = forth_stack_pop(ctx);
    size_t else_at, then_at;
    int truth;
    int result;

    if (condition == NULL) { stack_error("if", "stack underflow"); return -1; }
    if (as_truth(condition, &truth) != 0) {
        stack_error("if", "expects a boolean or integer condition");
        forth_stack_push(ctx, condition);
        return -1;
    }
    value_release(condition);
    if (find_if_end(program, *index + 1, last, &else_at, &then_at) != 0) {
        stack_error("if", "missing or duplicate else/then");
        return -1;
    }
    if (truth)
        result = execute_range(ctx, program, *index + 1, else_at == last ? then_at : else_at);
    else if (else_at != last)
        result = execute_range(ctx, program, else_at + 1, then_at);
    else
        result = 0;
    *index = then_at;
    return result;
}

static int execute_loop(Context *ctx, Value *program, size_t *index, size_t last) {
    size_t while_at, repeat_at;
    if (find_loop_end(program, *index + 1, last, &while_at, &repeat_at) != 0) {
        stack_error("begin", "expected `while ... repeat`");
        return -1;
    }
    for (;;) {
        Value *condition;
        int truth;
        if (execute_range(ctx, program, *index + 1, while_at) != 0)
            return -1;
        condition = forth_stack_pop(ctx);
        if (condition == NULL) { stack_error("while", "stack underflow"); return -1; }
        if (as_truth(condition, &truth) != 0) {
            stack_error("while", "expects a boolean or integer condition");
            forth_stack_push(ctx, condition);
            return -1;
        }
        value_release(condition);
        if (!truth) break;
        if (execute_range(ctx, program, while_at + 1, repeat_at) != 0)
            return -1;
    }
    *index = repeat_at;
    return 0;
}

static int execute_range(Context *ctx, Value *program, size_t first, size_t last) {
    size_t index;
    for (index = first; index < last; index++) {
        Value *token = program->as.list.items[index];
        Word *word;
        if (token->type != VALUE_SYMBOL) {
            value_retain(token);
            forth_stack_push(ctx, token);
            continue;
        }
        if (is_symbol(token, ":")) {
            Value *name;
            Value *body;
            size_t end, body_index;
            if (index + 1 >= last || program->as.list.items[index + 1]->type != VALUE_SYMBOL ||
                is_symbol(program->as.list.items[index + 1], ";")) {
                stack_error(":", "expected a word name");
                return -1;
            }
            name = program->as.list.items[index + 1];
            if (find_definition_end(program, index + 2, last, &end) != 0) {
                stack_error(":", "missing `;`");
                return -1;
            }
            body = value_new_list();
            for (body_index = index + 2; body_index < end; body_index++) {
                Value *body_token = program->as.list.items[body_index];
                value_retain(body_token);
                value_list_append(body, body_token);
            }
            forth_dictionary_add(ctx, name->as.string.data, NULL, body);
            index = end;
            continue;
        }
        if (is_symbol(token, "constant") || is_symbol(token, "value") || is_symbol(token, "variable")) {
            Value *name;
            Value *data;
            WordType type;
            if (index + 1 >= last || program->as.list.items[index + 1]->type != VALUE_SYMBOL) {
                stack_error(token->as.string.data, "expected a name");
                return -1;
            }
            name = program->as.list.items[index + 1];
            if (is_symbol(token, "variable")) {
                data = value_new_cell(value_new_int(0));
                type = WORD_VARIABLE;
            } else {
                data = forth_stack_pop(ctx);
                if (data == NULL) {
                    stack_error(token->as.string.data, "stack underflow");
                    return -1;
                }
                type = is_symbol(token, "constant") ? WORD_CONSTANT : WORD_VALUE;
            }
            forth_dictionary_add_data(ctx, name->as.string.data, type, data);
            index++;
            continue;
        }
        if (is_symbol(token, "to")) {
            Value *name;
            Value *data;
            Word *target;
            if (index + 1 >= last || program->as.list.items[index + 1]->type != VALUE_SYMBOL) {
                stack_error("to", "expected a value name");
                return -1;
            }
            name = program->as.list.items[index + 1];
            target = forth_dictionary_find(ctx, name->as.string.data);
            if (target == NULL || target->type != WORD_VALUE) {
                stack_error("to", "expects the name of a value");
                return -1;
            }
            data = forth_stack_pop(ctx);
            if (data == NULL) { stack_error("to", "stack underflow"); return -1; }
            value_release(target->data);
            target->data = data;
            index++;
            continue;
        }
        if (is_symbol(token, ".\"") || is_symbol(token, "s\"")) {
            Value *text;
            if (index + 1 >= last || program->as.list.items[index + 1]->type != VALUE_STRING) {
                stack_error(token->as.string.data, "expected a string");
                return -1;
            }
            text = program->as.list.items[++index];
            if (is_symbol(token, ".\"")) {
                fputs(text->as.string.data, stdout);
            } else {
                value_retain(text);
                forth_stack_push(ctx, text);
            }
            continue;
        }
        if (is_symbol(token, "if")) {
            int result = execute_if(ctx, program, &index, last);
            if (result != 0) return result;
            continue;
        }
        if (is_symbol(token, "begin")) {
            int result = execute_loop(ctx, program, &index, last);
            if (result != 0) return result;
            continue;
        }
        if (is_symbol(token, "do") || is_symbol(token, "?do")) {
            int result = execute_counted_loop(ctx, program, &index, last, is_symbol(token, "?do"));
            if (result != 0) return result;
            continue;
        }
        if (is_symbol(token, "leave"))
            return EXEC_LEAVE;
        if (is_symbol(token, "else") || is_symbol(token, "then") || is_symbol(token, "while") ||
            is_symbol(token, "repeat") || is_symbol(token, "loop") || is_symbol(token, "+loop") ||
            is_symbol(token, ";")) {
            stack_error(token->as.string.data, "unexpected control word");
            return -1;
        }
        word = forth_dictionary_find(ctx, token->as.string.data);
        if (word == NULL) {
            fprintf(stderr, "forth: unknown word: %s\n", token->as.string.data);
            return -1;
        }
        if (word->type == WORD_BUILTIN) {
            if (word->builtin(ctx, word->name) != 0) return -1;
        } else if (word->type == WORD_COLON) {
            int result = forth_execute(ctx, word->body);
            if (result != 0) return result;
        } else if (word->type == WORD_CONSTANT || word->type == WORD_VALUE || word->type == WORD_VARIABLE) {
            value_retain(word->data);
            forth_stack_push(ctx, word->data);
        }
    }
    return 0;
}

int forth_execute(Context *ctx, Value *program) {
    int result;
    if (program == NULL || program->type != VALUE_LIST) {
        fputs("forth: internal error: program is not a list\n", stderr);
        return -1;
    }
    if (ctx->execution_depth == FORTH_MAX_EXECUTION_DEPTH) {
        fputs("forth: execution nesting limit reached\n", stderr);
        return -1;
    }
    ctx->execution_depth++;
    result = execute_range(ctx, program, 0, program->as.list.length);
    if (ctx->execution_depth == 1 && result == EXEC_LEAVE) {
        stack_error("leave", "used outside a counted loop");
        result = -1;
    }
    ctx->execution_depth--;
    return result;
}

void forth_install_core_words(Context *ctx) {
    static const struct { const char *name; BuiltinFn function; } words[] = {
        { "+", builtin_math }, { "-", builtin_math }, { "*", builtin_math }, { "/", builtin_math },
        { "%", builtin_math }, { "mod", builtin_math }, { "/mod", builtin_divmod },
        { "negate", builtin_unary_math }, { "abs", builtin_unary_math }, { "1+", builtin_unary_math },
        { "1-", builtin_unary_math }, { "2*", builtin_unary_math }, { "2/", builtin_unary_math },
        { "min", builtin_min_max }, { "max", builtin_min_max },
        { "=", builtin_compare }, { "<>", builtin_compare }, { "<", builtin_compare }, { ">", builtin_compare },
        { "<=", builtin_compare }, { ">=", builtin_compare },
        { "0=", builtin_zero_compare }, { "0<>", builtin_zero_compare }, { "0<", builtin_zero_compare },
        { "0>", builtin_zero_compare },
        { ".", builtin_dot }, { ".s", builtin_dots }, { "cr", builtin_cr }, { "emit", builtin_emit },
        { "dup", builtin_dup }, { "drop", builtin_drop }, { "swap", builtin_swap }, { "over", builtin_over },
        { "rot", builtin_rot }, { "-rot", builtin_minus_rot }, { "?dup", builtin_qdup },
        { "2dup", builtin_two_stack }, { "2drop", builtin_two_stack }, { "2swap", builtin_two_stack },
        { "2over", builtin_two_stack }, { "nip", builtin_nip }, { "tuck", builtin_tuck },
        { "pick", builtin_pick_roll }, { "roll", builtin_pick_roll },
        { "clear", builtin_clear }, { "depth", builtin_depth },
        { "not", builtin_boolean }, { "and", builtin_bitwise }, { "or", builtin_bitwise },
        { "xor", builtin_bitwise }, { "invert", builtin_bitwise }, { "lshift", builtin_bitwise },
        { "rshift", builtin_bitwise },
        { ">r", builtin_return_stack }, { "r>", builtin_return_stack }, { "r@", builtin_return_stack },
        { "@", builtin_fetch }, { "!", builtin_store }, { "+!", builtin_add_store }, { "?", builtin_question },
        { "i", builtin_loop_index }, { "j", builtin_loop_index },
        { "words", builtin_words }
    };
    size_t index;
    for (index = 0; index < sizeof(words) / sizeof(words[0]); index++)
        forth_dictionary_add(ctx, words[index].name, words[index].function, NULL);
}
