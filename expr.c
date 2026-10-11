#include "expr.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

typedef enum {
    VAR_PLAYER_X, VAR_PLAYER_Y, VAR_PLAYER_SX, VAR_PLAYER_SY,
    VAR_PLAYER_DEATH, VAR_IS_JUMPING, VAR_IS_FALLING, VAR_KEY_UP,
    VAR_FLIPPED, VAR_PLAYER_STATE, VAR_PLAYER_WALL, VAR_DIRECTION,
    VAR_FRICTION_DX, VAR_FRICTION_DY, VAR_FRICTION, VAR_FRAME,
    VAR_COUNT
} VarId;

static const char *VAR_NAMES[VAR_COUNT] = {
    "PLAYER_X", "PLAYER_Y", "PLAYER_SX", "PLAYER_SY",
    "PLAYER_DEATH", "is_jumping", "is_falling", "KEY_UP",
    "flipped", "player_state", "player_wall", "direction",
    "friction_dx", "friction_dy", "friction", "frame"
};

typedef enum {
    FN_ABS, FN_MIN, FN_MAX, FN_FLOOR, FN_CEIL, FN_ROUND, FN_SQRT, FN_POW,
    FN_SIGN,
    FN_COUNT
} FuncId;

static const struct { const char *name; FuncId id; int argc; } FUNCS[] = {
    { "abs",   FN_ABS,   1 },
    { "min",   FN_MIN,   2 },
    { "max",   FN_MAX,   2 },
    { "floor", FN_FLOOR, 1 },
    { "ceil",  FN_CEIL,  1 },
    { "round", FN_ROUND, 1 },
    { "sqrt",  FN_SQRT,  1 },
    { "pow",   FN_POW,   2 },
    { "sign",  FN_SIGN,  1 },
};
#define FUNCS_COUNT (int)(sizeof(FUNCS) / sizeof(FUNCS[0]))

typedef enum {
    N_NUM, N_VAR, N_NEG, N_NOT,
    N_ADD, N_SUB, N_MUL, N_DIV,
    N_EQ, N_NE, N_LT, N_GT, N_LE, N_GE,
    N_AND, N_OR, N_CALL
} NodeType;

struct Expr {
    NodeType type;
    double   num;
    VarId    var;
    FuncId   func;
    Expr    *a, *b;
};


typedef enum {
    T_NUM, T_IDENT, T_OP, T_LPAREN, T_RPAREN, T_COMMA, T_END
} TokType;

typedef struct {
    TokType type;
    double  num;
    char    text[32];
} Token;

typedef struct {
    const char *src;
    Token       cur;
} Lexer;

static void lex_advance(Lexer *lx)
{
    const char *p = lx->src;
    while (*p && isspace((unsigned char)*p)) p++;

    if (!*p) { lx->cur.type = T_END; lx->src = p; return; }

    if (isdigit((unsigned char)*p) || (*p == '.' && isdigit((unsigned char)p[1]))) {
        char *end;
        lx->cur.num = strtod(p, &end);
        lx->cur.type = T_NUM;
        lx->src = end;
        return;
    }

    if (isalpha((unsigned char)*p) || *p == '_') {
        int n = 0;
        while ((isalnum((unsigned char)*p) || *p == '_') && n < (int)sizeof(lx->cur.text) - 1) {
            lx->cur.text[n++] = *p++;
        }
        if (*p == '.' && (isalpha((unsigned char)p[1]) || p[1] == '_') && n < (int)sizeof(lx->cur.text) - 1) {
            lx->cur.text[n++] = *p++;
            while ((isalnum((unsigned char)*p) || *p == '_') && n < (int)sizeof(lx->cur.text) - 1) {
                lx->cur.text[n++] = *p++;
            }
        }
        lx->cur.text[n] = '\0';
        lx->cur.type = T_IDENT;
        lx->src = p;
        return;
    }

    if (*p == '(') { lx->cur.type = T_LPAREN; lx->src = p + 1; return; }
    if (*p == ')') { lx->cur.type = T_RPAREN; lx->src = p + 1; return; }
    if (*p == ',') { lx->cur.type = T_COMMA;  lx->src = p + 1; return; }

    static const char *ops[] = { "&&", "||", "==", "!=", "<=", ">=", "<", ">", "+", "-", "*", "/", "!", NULL };
    for (int i = 0; ops[i]; i++) {
        size_t len = strlen(ops[i]);
        if (strncmp(p, ops[i], len) == 0) {
            lx->cur.type = T_OP;
            strncpy(lx->cur.text, ops[i], sizeof(lx->cur.text) - 1);
            lx->cur.text[sizeof(lx->cur.text) - 1] = '\0';
            lx->src = p + len;
            return;
        }
    }

    lx->src = p + 1;
    lex_advance(lx);
}


typedef struct {
    Lexer lx;
    bool  error;
} Parser;

static Expr *node_new(NodeType t) { Expr *e = calloc(1, sizeof(Expr)); e->type = t; return e; }
static bool tok_is_op(const Parser *p, const char *s) { return p->lx.cur.type == T_OP && strcmp(p->lx.cur.text, s) == 0; }

static Expr *parse_or(Parser *p);

static Expr *parse_primary(Parser *p)
{
    if (p->lx.cur.type == T_NUM) {
        Expr *e = node_new(N_NUM);
        e->num = p->lx.cur.num;
        lex_advance(&p->lx);
        return e;
    }
    if (p->lx.cur.type == T_IDENT) {
        if (strcmp(p->lx.cur.text, "true") == 0) {
            Expr *e = node_new(N_NUM); e->num = 1.0; lex_advance(&p->lx); return e;
        }
        if (strcmp(p->lx.cur.text, "false") == 0) {
            Expr *e = node_new(N_NUM); e->num = 0.0; lex_advance(&p->lx); return e;
        }

        const char *fname = p->lx.cur.text;
        if (strncmp(fname, "Math.", 5) == 0) fname += 5;
        for (int i = 0; i < FUNCS_COUNT; i++) {
            if (strcmp(fname, FUNCS[i].name) != 0) continue;

            char saved_text[32];
            strncpy(saved_text, p->lx.cur.text, sizeof(saved_text));
            lex_advance(&p->lx);
            if (p->lx.cur.type != T_LPAREN) {
                fprintf(stderr, "expr: expected '(' after '%s'\n", saved_text);
                p->error = true;
                return node_new(N_NUM);
            }
            lex_advance(&p->lx);

            Expr *e = node_new(N_CALL);
            e->func = FUNCS[i].id;
            e->a = parse_or(p);
            if (FUNCS[i].argc == 2) {
                if (p->lx.cur.type != T_COMMA) {
                    fprintf(stderr, "expr: '%s' expects 2 arguments\n", saved_text);
                    p->error = true;
                } else {
                    lex_advance(&p->lx);
                    e->b = parse_or(p);
                }
            }
            if (p->lx.cur.type != T_RPAREN) {
                fprintf(stderr, "expr: expected ')' after '%s(...)'\n", saved_text);
                p->error = true;
            } else {
                lex_advance(&p->lx);
            }
            return e;
        }

        for (int i = 0; i < VAR_COUNT; i++) {
            if (strcmp(p->lx.cur.text, VAR_NAMES[i]) == 0) {
                Expr *e = node_new(N_VAR);
                e->var = (VarId)i;
                lex_advance(&p->lx);
                return e;
            }
        }
        fprintf(stderr, "expr: unknown identifier '%s'\n", p->lx.cur.text);
        p->error = true;
        lex_advance(&p->lx);
        return node_new(N_NUM);
    }
    if (p->lx.cur.type == T_LPAREN) {
        lex_advance(&p->lx);
        Expr *inner = parse_or(p);
        if (p->lx.cur.type != T_RPAREN) { fprintf(stderr, "expr: expected ')'\n"); p->error = true; }
        else lex_advance(&p->lx);
        return inner;
    }
    fprintf(stderr, "expr: unexpected token\n");
    p->error = true;
    return node_new(N_NUM);
}

static Expr *parse_unary(Parser *p)
{
    if (tok_is_op(p, "-")) { lex_advance(&p->lx); Expr *e = node_new(N_NEG); e->a = parse_unary(p); return e; }
    if (tok_is_op(p, "!")) { lex_advance(&p->lx); Expr *e = node_new(N_NOT); e->a = parse_unary(p); return e; }
    return parse_primary(p);
}

static Expr *parse_mul(Parser *p)
{
    Expr *left = parse_unary(p);
    while (tok_is_op(p, "*") || tok_is_op(p, "/")) {
        NodeType t = tok_is_op(p, "*") ? N_MUL : N_DIV;
        lex_advance(&p->lx);
        Expr *e = node_new(t); e->a = left; e->b = parse_unary(p); left = e;
    }
    return left;
}

static Expr *parse_add(Parser *p)
{
    Expr *left = parse_mul(p);
    while (tok_is_op(p, "+") || tok_is_op(p, "-")) {
        NodeType t = tok_is_op(p, "+") ? N_ADD : N_SUB;
        lex_advance(&p->lx);
        Expr *e = node_new(t); e->a = left; e->b = parse_mul(p); left = e;
    }
    return left;
}

static Expr *parse_rel(Parser *p)
{
    Expr *left = parse_add(p);
    while (tok_is_op(p, "<") || tok_is_op(p, ">") || tok_is_op(p, "<=") || tok_is_op(p, ">=")) {
        NodeType t = tok_is_op(p, "<") ? N_LT : tok_is_op(p, ">") ? N_GT : tok_is_op(p, "<=") ? N_LE : N_GE;
        lex_advance(&p->lx);
        Expr *e = node_new(t); e->a = left; e->b = parse_add(p); left = e;
    }
    return left;
}

static Expr *parse_eq(Parser *p)
{
    Expr *left = parse_rel(p);
    while (tok_is_op(p, "==") || tok_is_op(p, "!=")) {
        NodeType t = tok_is_op(p, "==") ? N_EQ : N_NE;
        lex_advance(&p->lx);
        Expr *e = node_new(t); e->a = left; e->b = parse_rel(p); left = e;
    }
    return left;
}

static Expr *parse_and(Parser *p)
{
    Expr *left = parse_eq(p);
    while (tok_is_op(p, "&&")) {
        lex_advance(&p->lx);
        Expr *e = node_new(N_AND); e->a = left; e->b = parse_eq(p); left = e;
    }
    return left;
}

static Expr *parse_or(Parser *p)
{
    Expr *left = parse_and(p);
    while (tok_is_op(p, "||")) {
        lex_advance(&p->lx);
        Expr *e = node_new(N_OR); e->a = left; e->b = parse_and(p); left = e;
    }
    return left;
}

Expr *expr_compile(const char *text)
{
    if (!text || !*text) text = "PLAYER_X";

    Parser p = { { text, {0} }, false };
    lex_advance(&p.lx);
    Expr *root = parse_or(&p);

    if (p.lx.cur.type != T_END) {
        fprintf(stderr, "expr: trailing input after expression: '%s'\n", p.lx.cur.text);
        p.error = true;
    }
    if (p.error) { expr_free(root); return NULL; }
    return root;
}

void expr_free(Expr *e)
{
    if (!e) return;
    expr_free(e->a);
    expr_free(e->b);
    free(e);
}

static double var_value(VarId v, const ExprCtx *ctx)
{
    const PlayerState *ps = ctx->ps;
    switch (v) {
        case VAR_PLAYER_X:      return (double)ps->PLAYER_X;
        case VAR_PLAYER_Y:      return (double)ps->PLAYER_Y;
        case VAR_PLAYER_SX:     return (double)ps->PLAYER_SX;
        case VAR_PLAYER_SY:     return (double)ps->PLAYER_SY;
        case VAR_PLAYER_DEATH:  return ps->PLAYER_DEATH ? 1.0 : 0.0;
        case VAR_IS_JUMPING:    return (double)ps->is_jumping;
        case VAR_IS_FALLING:    return (double)ps->is_falling;
        case VAR_KEY_UP:        return (double)ps->KEY_UP;
        case VAR_FLIPPED:       return (double)ps_get_flipped(ps);
        case VAR_PLAYER_STATE:  return (double)ps->player_state;
        case VAR_PLAYER_WALL:   return (double)ps_get_wall(ps);
        case VAR_DIRECTION:     return ps_get_direction(ps);
        case VAR_FRICTION_DX:   return ctx->ap ? ctx->ap->friction_dx : 0.0;
        case VAR_FRICTION_DY:   return ctx->ap ? ctx->ap->friction_dy : 0.0;
        case VAR_FRICTION:      return ctx->ap ? ctx->ap->friction    : 0.0;
        case VAR_FRAME:         return (double)ctx->frame;
        default:                return 0.0;
    }
}

double expr_eval(const Expr *e, const ExprCtx *ctx)
{
    switch (e->type) {
        case N_NUM: return e->num;
        case N_VAR: return var_value(e->var, ctx);
        case N_NEG: return -expr_eval(e->a, ctx);
        case N_NOT: return (expr_eval(e->a, ctx) == 0.0) ? 1.0 : 0.0;
        case N_ADD: return expr_eval(e->a, ctx) + expr_eval(e->b, ctx);
        case N_SUB: return expr_eval(e->a, ctx) - expr_eval(e->b, ctx);
        case N_MUL: return expr_eval(e->a, ctx) * expr_eval(e->b, ctx);
        case N_DIV: return expr_eval(e->a, ctx) / expr_eval(e->b, ctx);
        case N_EQ:  return (expr_eval(e->a, ctx) == expr_eval(e->b, ctx)) ? 1.0 : 0.0;
        case N_NE:  return (expr_eval(e->a, ctx) != expr_eval(e->b, ctx)) ? 1.0 : 0.0;
        case N_LT:  return (expr_eval(e->a, ctx) <  expr_eval(e->b, ctx)) ? 1.0 : 0.0;
        case N_GT:  return (expr_eval(e->a, ctx) >  expr_eval(e->b, ctx)) ? 1.0 : 0.0;
        case N_LE:  return (expr_eval(e->a, ctx) <= expr_eval(e->b, ctx)) ? 1.0 : 0.0;
        case N_GE:  return (expr_eval(e->a, ctx) >= expr_eval(e->b, ctx)) ? 1.0 : 0.0;
        case N_AND: return (expr_eval(e->a, ctx) != 0.0 && expr_eval(e->b, ctx) != 0.0) ? 1.0 : 0.0;
        case N_OR:  return (expr_eval(e->a, ctx) != 0.0 || expr_eval(e->b, ctx) != 0.0) ? 1.0 : 0.0;
        case N_CALL: {
            double a = expr_eval(e->a, ctx);
            switch (e->func) {
                case FN_ABS:   return fabs(a);
                case FN_MIN:   return fmin(a, expr_eval(e->b, ctx));
                case FN_MAX:   return fmax(a, expr_eval(e->b, ctx));
                case FN_FLOOR: return floor(a);
                case FN_CEIL:  return ceil(a);
                case FN_ROUND: return round(a);
                case FN_SQRT:  return sqrt(a);
                case FN_POW:   return pow(a, expr_eval(e->b, ctx));
                case FN_SIGN:  return (a > 0.0) ? 1.0 : (a < 0.0) ? -1.0 : 0.0;
                default:       return 0.0;
            }
        }
    }
    return 0.0;
}
