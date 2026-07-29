#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>

/* -------------------------------------- DATA STRUCTURE --------------------------------------*/

typedef enum{
    TYPE_INT,
    TYPE_FLOAT,
    TYPE_BOOL,
    TYPE_STR,
    TYPE_LIST,
    TYPE_SYMBOL,
    TYPE_ALL
}TYPE;

typedef struct object{
    int refcount;
    TYPE type;
    union{
        int i;
        float f;
        struct {
            char *buf;
            size_t len;
        }str;
        struct {
            struct object **ele;
            size_t len;
        }list;
    };
}object;

typedef struct {
    char *program;
    char *p;
}parserObj;

struct context;
typedef struct word {
    char *name;
    int (*callback) (struct context *ctx, char *spec);
    object *body;
} word;

typedef struct {
    word *words;
    size_t count;
    size_t capacity;
} dictionary;

typedef struct context{
    object *stack;
    dictionary dict;
} context;

/* -------------------------------------- MEMORY MANAGMENT -------------------------------------- */

void *xmalloc(size_t size) {
    void *ptr = malloc(size);
    if (ptr == NULL) {
        fprintf(stderr, "Out of memory allocating %zu bytes\n",size);
        exit(1);
    }
    return ptr;
}

void *xrealloc(void *oldptr, size_t size) {
    void *ptr = realloc(oldptr, size);
    if (ptr == NULL) {
        fprintf(stderr, "Out of memory allocating %zu bytes\n", size);
        exit(1);
    }
    return ptr;
}

void freeObject(object *o);

void retain(object *o) {
    if (!o) return;
    o->refcount++;
}
/* Decrements refcount and frees the object if it reaches zero. 
 * Recursively releases nested objects (lists, strings). */
void release(object *o) {
    if (!o) return;
    if (o->refcount <= 0) {
        fprintf(stderr, "Error: trying to release an object with refcount <= 0 (already freed or invalid)\n");
        exit(1);
    }
    o->refcount--;
    if (o->refcount == 0) freeObject(o);
}

void freeObject(object *o) {
    if (o->type == TYPE_LIST){
        for (size_t i = 0; i < o->list.len; ++i){
            release(o->list.ele[i]);
        }
        free(o->list.ele);
    }else if (o->type == TYPE_STR || o->type == TYPE_SYMBOL){
        free(o->str.buf);
    }
    free(o);
}

void freeContext(context *ctx){
    if (!ctx) return;
    release(ctx->stack);
    for (size_t i = 0; i < ctx->dict.count; i++) {
        free(ctx->dict.words[i].name);
        if (ctx->dict.words[i].body) release(ctx->dict.words[i].body);
    }
    free(ctx->dict.words);
    free(ctx);
}

/* -------------------------------------- CREATE OBJECT FUNCTION -------------------------------------- */

object *createObject(TYPE type){
    object *o = xmalloc(sizeof(object));
    o->type = type;
    o->refcount = 1;
    return o;
}

object *createIntObject(int i){
    object *o = createObject(TYPE_INT);
    o->i = i;
    return o;
}

object *createFloatObject(float i){
    object *o = createObject(TYPE_FLOAT);
    o->f = i;
    return o;
}

object *createBoolObject(int i){
    object *o = createObject(TYPE_BOOL);
    o->i = i;
    return o;
}

object *createStrObject(char *str, size_t len){
    object *o = createObject(TYPE_STR);
    o->str.buf =  xmalloc(len+1);
    o->str.len = len;
    memcpy(o->str.buf, str, len);
    o->str.buf[len] = 0;
    return o;
}

object *createSymbolObject(char *str, size_t len) {
    object *o = createStrObject(str, len);
    o->type = TYPE_SYMBOL;
    return o;
}

/* -------------------------------------- LIST AND STACK MANAGMENT -------------------------------------- */

object *createListObject() {
    object *o = createObject(TYPE_LIST);
    o->list.ele = NULL;
    o->list.len = 0;
    return o;
}

void listPush(object *l, object *ele) {
    l->list.ele = xrealloc(l->list.ele, sizeof(object*) * (l->list.len+1));
    l->list.ele[l->list.len] = ele;
    l->list.len++;
}

object *listPopType(context *ctx, TYPE type) {
    object *stack = ctx->stack;
    if (stack->list.len == 0) return NULL;
    object *toPop = stack->list.ele[stack->list.len-1];
    if (type != TYPE_ALL && toPop->type != type) return NULL;

    stack->list.len--;
    if (stack->list.len == 0) {
        free(stack->list.ele);
        stack->list.ele = NULL;
    } else {
        stack->list.ele = xrealloc(stack->list.ele, sizeof(object*) * (stack->list.len));
    }
    return toPop;
}

object *listPop(context *ctx) {
    return listPopType(ctx, TYPE_ALL);
}

object *listPeek(context *ctx){
    if (ctx->stack->list.len == 0) return NULL;
    return ctx->stack->list.ele[ctx->stack->list.len-1];
}

void parseSpaces(parserObj *parser){
    while(isspace((unsigned char)parser->p[0])) parser->p++;
}

void stackPush(context *ctx, object *ele){
    listPush(ctx->stack, ele);
}

object *stackPop(context *ctx, TYPE type){
    return listPopType(ctx, type);
}

object *stackPeek(context *ctx){
    return listPeek(ctx);
}

context *createContext(){
    context *context = xmalloc(sizeof(*context));
    context->stack = createListObject();
    context->dict.words = NULL;
    context->dict.count = 0;
    context->dict.capacity = 0;
    return context;
}

/* -------------------------------------- PARSER FUNCTION -------------------------------------- */

void printObject(object *o);

#define MAX_NUM_LEN 128
object *parseNumber(parserObj *parser) {
    char buf[MAX_NUM_LEN];
    char *start = parser->p;
    char *end;
    int isFloat = 0;

    if (parser->p[0] == '-')
        parser->p++;

    while (parser->p[0] && (isdigit((unsigned char)parser->p[0]) || (parser->p[0] == '.' && !isFloat))) {
        if (parser->p[0] == '.') {
            isFloat = 1;
        }
        parser->p++;
    }

    end = parser->p;
    int numlen = end - start;

    if (numlen >= MAX_NUM_LEN) return NULL;

    memcpy(buf, start, numlen);
    buf[numlen] = '\0';

    if (isFloat) {
        return createFloatObject(atof(buf));
    }

    return createIntObject(atoi(buf));
}

#define MAX_STR_LEN 1024
object *parserString(parserObj *parser) {
    if (!parser || parser->p[0] != '\"') {
        return NULL;
    }
    parser->p++;
    char *start = parser->p;

    while (parser->p[0] != '\0' && parser->p[0] != '\"') {
        parser->p++;
    }

    if (parser->p[0] == '\0') {
        return NULL;
    }

    size_t str_len = parser->p - start;

    if (str_len >= MAX_STR_LEN) {
        return NULL;
    }

    char buf[MAX_STR_LEN];
    memcpy(buf, start, str_len);
    buf[str_len] = '\0';
    parser->p++;
    return createStrObject(buf, str_len);
}

int is_symbol_char(int c) {
    char symchars[] = "+-*/%.:;=<>!?";
    if (isalpha(c)) {
        return 1;
    }else if (strchr(symchars,c) != NULL) {
        return 1;
    } else {
        return 0;
    }
}

int compareStringObject(object *a, object *b){
    size_t minlen = a->str.len < b->str.len ? a->str.len : b->str.len;
    int cmp = memcmp(a->str.buf, b->str.buf, minlen);

    if (cmp == 0) {
        if (a->str.len == b->str.len) return 0;
        else if (a->str.len > b->str.len) return 1;
        else return -1;
    }else {
        if (cmp < 0) return -1;
        else return 1;
    }
}

object *parseSymbol(parserObj *parser){
    char *start = parser->p;
    while(parser->p[0] && is_symbol_char((unsigned char)parser->p[0])) parser->p++;
    int len = parser->p - start;
    return createSymbolObject(start, len);
}

/* -------------------------------------- LEXER AND COMPILER -------------------------------------- */
/* Converts raw source code into an AST represented as a list of objects 
 * Parses the token stream and builds the flat program AST.
 * Returns NULL on syntax error. */
object *compile(char *prg){
    parserObj parser;
    parser.program = prg;
    parser.p = prg;

    object *parsed = createListObject();

    while(parser.p) {
        object *o;
        char *start = parser.p;

        parseSpaces(&parser);
        if (parser.p[0] == 0) break;

        if (isdigit((unsigned char)parser.p[0]) ||
            (parser.p[0] == '-' && isdigit((unsigned char)parser.p[1])) ||
            (parser.p[0] == '.' && isdigit((unsigned char)parser.p[1]))) {
            o = parseNumber(&parser);
        } else if (parser.p[0] == '\"') {
            o = parserString(&parser);
        } else if (is_symbol_char((unsigned char)parser.p[0])){
            o = parseSymbol(&parser);
        }else {
            o = NULL;
        }

        if (o == NULL) {
            release(parsed);
            printf("Syntax error near: %.32s ...\n", start);
            return NULL;
        }else {
            listPush(parsed, o);
        }
    }
    return parsed;
}

/* -------------------------------------- FUNCTION -------------------------------------- */

void registerWord(context *ctx, char *name, int (*callback)(context *ctx, char *spec), object *body) {
    if (ctx->dict.count == ctx->dict.capacity) {
        size_t newCapacity =ctx->dict.capacity == 0 ? 16 : ctx->dict.capacity * 2;
        ctx->dict.words = xrealloc(ctx->dict.words, sizeof(word) * newCapacity);
        ctx->dict.capacity = newCapacity;
    }

    word *w = &ctx->dict.words[ctx->dict.count];

    w->name = xmalloc(strlen(name)+1);
    strcpy(w->name,name);

    w->callback = callback;
    w->body = body;

    ctx->dict.count++;
}

/* Performs a reverse lookup in the dictionary to support word shadowing. */
word *lookup(context *ctx, char *name){
    for(int i = (int)ctx->dict.count-1; i >= 0; i--) {
        if(strcmp(ctx->dict.words[i].name,name)==0)
            return &ctx->dict.words[i];

    }

    return NULL;
}

int isNumber(object *o) {
    return o && (o->type == TYPE_INT || o->type == TYPE_FLOAT);
}

float getFloatValue(object *o){
    if(o->type == TYPE_INT)
        return (float)o->i;

    return o->f;
}

object *createMathResult(float value, object *a, object *b) {
    if(a->type == TYPE_INT && b->type == TYPE_INT) {
        return createIntObject((int)value);
    }

    return createFloatObject(value);
}

/* FORTH BUILT-IN PRIMITIVES
 * Core stack manipulations, math operations, and comparison functions. */
int forthBasicMathFunction(context *ctx, char *op){
    object *b = stackPop(ctx, TYPE_ALL);
    if(!b) {
        fprintf(stderr, "Error: stack underflow for '%s'\n", op);
        return -1;
    }
    if(!isNumber(b)) {
        fprintf(stderr, "Error: '%s' requires a numeric operand\n", op);
        stackPush(ctx, b);
        return -1;
    }

    object *a = stackPop(ctx, TYPE_ALL);
    if(!a) {
        fprintf(stderr, "Error: stack underflow for '%s'\n", op);
        stackPush(ctx, b);
        return -1;
    }
    if(!isNumber(a)) {
        fprintf(stderr, "Error: '%s' requires a numeric operand\n", op);
        stackPush(ctx, a);
        stackPush(ctx, b);
        return -1;
    }

    float av = getFloatValue(a);
    float bv = getFloatValue(b);

    float result;

    if(strcmp(op, "+") == 0)
        result = av + bv;

    else if(strcmp(op, "-") == 0)
        result = av - bv;

    else if(strcmp(op, "*") == 0)
        result = av * bv;

    else if(strcmp(op, "/") == 0) {
        if(bv == 0)
        {
            fprintf(stderr,"Error: divide by zero\n");

            stackPush(ctx,a);
            stackPush(ctx,b);

            return -1;
        }

        result = av / bv;
    } else {
        release(a);
        release(b);
        return -1;
    }
    stackPush(ctx, createMathResult(result,a,b));

    release(a);
    release(b);

    return 0;
}

int forthCompare(context *ctx, char *op){
    object *b = stackPop(ctx, TYPE_ALL);
    if(!b) {
        fprintf(stderr, "Error: stack underflow for '%s'\n", op);
        return -1;
    }
    object *a = stackPop(ctx, TYPE_ALL);
    if(!a) {
        fprintf(stderr, "Error: stack underflow for '%s'\n", op);
        stackPush(ctx, b);
        return -1;
    }

    int result;
    if (isNumber(a) && isNumber(b)) {
        float av = getFloatValue(a), bv = getFloatValue(b);
        if(strcmp(op,"=")==0) result = (av == bv);
        else if(strcmp(op,"<")==0) result = (av < bv);
        else if(strcmp(op,">")==0) result = (av > bv);
        else { release(a); release(b); return -1; }
    } else if ((a->type == TYPE_STR || a->type == TYPE_SYMBOL) && (b->type == TYPE_STR || b->type == TYPE_SYMBOL)) {
        int cmp = compareStringObject(a,b);
        if(strcmp(op,"=")==0) result = (cmp == 0);
        else if(strcmp(op,"<")==0) result = (cmp < 0);
        else if(strcmp(op,">")==0) result = (cmp > 0);
        else { release(a); release(b); return -1; }
    } else {
        fprintf(stderr, "Error: '%s' requires two comparable operands\n", op);
        release(a);
        release(b);
        return -1;
    }

    stackPush(ctx, createBoolObject(result));
    release(a);
    release(b);
    return 0;
}

int forthDot(context *ctx, char *n){
    (void)n;
    object *o = stackPop(ctx,TYPE_ALL);

    if(!o) {
        fprintf(stderr, "Error: stack underflow for '.'\n");
        return -1;
    }

    printObject(o);
    printf(" ");
    release(o);
    return 0;
}

int forthDots(context *ctx, char *n){
    (void)n;
    printObject(ctx->stack);
    printf("\n");
    return 0;
}

int forthDup(context *ctx, char *n){
    (void)n;
    object *a = stackPeek(ctx);
    if (!a) {
        fprintf(stderr, "Error: stack underflow for 'dup'\n");
        return -1;
    }
    retain(a);
    stackPush(ctx, a);
    return 0;
}

/* NUOVE word di utilità elementari, comuni in ogni Forth minimale */
int forthDrop(context *ctx, char *n){
    (void)n;
    object *a = stackPop(ctx, TYPE_ALL);
    if (!a) {
        fprintf(stderr, "Error: stack underflow for 'drop'\n");
        return -1;
    }
    release(a);
    return 0;
}

int forthSwap(context *ctx, char *n){
    (void)n;
    object *b = stackPop(ctx, TYPE_ALL);
    if (!b) {
        fprintf(stderr, "Error: stack underflow for 'swap'\n");
        return -1;
    }
    object *a = stackPop(ctx, TYPE_ALL);
    if (!a) {
        fprintf(stderr, "Error: stack underflow for 'swap'\n");
        stackPush(ctx, b);
        return -1;
    }
    stackPush(ctx, b);
    stackPush(ctx, a);
    return 0;
}

void initForth(context *ctx) {
    registerWord(ctx,"+",forthBasicMathFunction,NULL);
    registerWord(ctx,"-",forthBasicMathFunction,NULL);
    registerWord(ctx,"*",forthBasicMathFunction,NULL);
    registerWord(ctx,"/",forthBasicMathFunction,NULL);
    registerWord(ctx,"=",forthCompare,NULL);
    registerWord(ctx,"<",forthCompare,NULL);
    registerWord(ctx,">",forthCompare,NULL);
    registerWord(ctx,".",forthDot,NULL);
    registerWord(ctx,".s",forthDots,NULL);
    registerWord(ctx,"dup",forthDup, NULL);
    registerWord(ctx,"drop",forthDrop, NULL);
    registerWord(ctx,"swap",forthSwap, NULL);
}

/* -------------------------------------- EXEC -------------------------------------- */

/* Evaluates the compiled AST sequentially, executing primitives or words.*/
int exec(context *ctx, object *prg) {
    for(size_t i = 0; i < prg->list.len; i++) {
        object *o = prg->list.ele[i];
        switch(o->type) {

        case TYPE_INT:
        case TYPE_FLOAT:
        case TYPE_BOOL:
        case TYPE_STR:
            retain(o);
            stackPush(ctx,o);
            break;
        case TYPE_SYMBOL:
        {
            if (strcmp(o->str.buf, ":") == 0) {
                if (i+1 >= prg->list.len || prg->list.ele[i+1]->type != TYPE_SYMBOL) {
                    fprintf(stderr, "Error: ':' must be followed by a word name\n");
                    return -1;
                }
                char *name = prg->list.ele[i+1]->str.buf;

                size_t j = i+2;
                int found = 0;
                object *body = createListObject();
                for (; j < prg->list.len; j++) {
                    object *cur = prg->list.ele[j];
                    if (cur->type == TYPE_SYMBOL && strcmp(cur->str.buf,";")==0) {
                        found = 1;
                        break;
                    }
                    retain(cur);
                    listPush(body, cur);
                }

                if (!found) {
                    fprintf(stderr, "Error: missing ';' to close definition of '%s'\n", name);
                    release(body);
                    return -1;
                }

                registerWord(ctx, name, NULL, body);
                i = j;
                break;
            }

            word *w = lookup(ctx,o->str.buf);
            if(!w){
                fprintf(stderr, "Unknown word: %s\n", o->str.buf);
                return -1;
            }

            if(w->callback) {
                if(w->callback(ctx,o->str.buf) != 0)
                    return -1;
            }
            else if(w->body) {
                if(exec(ctx,w->body)!=0)
                    return -1;
            }
            break;
        }
        default:
            fprintf(stderr, "Error: cannot execute object of type %d\n", o->type);
            return -1;
        }
    }

    return 0;
}

void printObject(object *o){
    switch(o->type){
    case TYPE_BOOL:
        printf(o->i ? "true " : "false ");
        break;
    case TYPE_INT:
        printf("%d ", o->i);
        break;
    case TYPE_FLOAT:
        printf("%.3f ", o->f);
        break;
    case TYPE_STR:
        printf("\"%s\" ", o->str.buf);
        break;
    case TYPE_SYMBOL:
        printf("%s", o->str.buf);
        break;
    case TYPE_LIST:
        printf("[");
        for (size_t j = 0; j < o->list.len; j++) {
            object *ele = o->list.ele[j];
            printObject(ele);
            if (j != o->list.len-1) printf(" ");
        }
        printf("]");
        break;
    default:
        printf("?");
        break;
    }

}

/* -------------------------------------- REPL --------------------------------------*/

#define MAX_LEN 1024
int inlineProgram(context *ctx){
    int c;
    size_t len;
    object *prg;
    char *buf = xmalloc(sizeof(char) * MAX_LEN);

    while (1){
        len = 0;
        printf(">> ");
        fflush(stdout);

        while ((c = getchar()) != '\n' && c != EOF && len < MAX_LEN - 1){
            buf[len++] = (char)c;
        }

        if (len == MAX_LEN - 1 && c != '\n' && c != EOF) {
            while ((c = getchar()) != '\n' && c != EOF) {}
            fprintf(stderr, "Warning: input line truncated to %d chars\n", MAX_LEN - 1);
        }

        if (c == EOF && len == 0){
            printf("\n");
            break;
        }

        buf[len] = '\0';

        if (strcmp(buf, "exit") == 0 || strcmp(buf, "quit") == 0){
            break;
        }

        prg = compile(buf);
        if (prg != NULL){
            exec(ctx, prg);       
            release(prg);            
        }

        if (c == EOF){
            break;
        }
    }

    free(buf);
    return 0;
}

/* -------------------------------------- MAIN --------------------------------------*/

int main(int argc, char **argv) {
    if (argc != 2) {
        context *ctx = createContext();
        initForth(ctx);
        int rc = inlineProgram(ctx);
        freeContext(ctx);
        return rc == 0 ? 0 : 1;
    }

    /*Reading program memory , for later parsing*/
    FILE *fp = fopen(argv[1], "r");
    if (fp == NULL) {
        perror("Opening Toy Forth program");
        return 1;
    }

    if (fseek(fp, 0, SEEK_END) != 0) {
        perror("fseek");
        fclose(fp);
        return 1;
    }
    long file_size = ftell(fp);
    if (file_size < 0) {
        perror("ftell");
        fclose(fp);
        return 1;
    }
    char *prgtext = xmalloc(file_size+1);
    fseek(fp, 0, SEEK_SET);
    size_t read = fread(prgtext, 1, file_size, fp);
    fclose(fp);
    if ((long)read != file_size) {
        fprintf(stderr, "Error: could not read whole file (%zu/%ld bytes)\n", read, file_size);
        free(prgtext);
        return 1;
    }
    prgtext[file_size] = 0;

    object *prg = compile(prgtext);
    free(prgtext);
    if (prg == NULL) {
        return 1;
    }

    printObject(prg);
    printf("\n");

    context *ctx = createContext();
    initForth(ctx);

    int rc = exec(ctx, prg);

    printf("Stack content at end: ");
    printObject(ctx->stack);
    printf("\n");

    release(prg);
    freeContext(ctx);

    return rc == 0 ? 0 : 1;
}