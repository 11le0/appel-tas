#ifndef EXPR_H
#define EXPR_H

#include <stdbool.h>
#include "physics.h"


typedef struct Expr Expr;

Expr *expr_compile(const char *text);
void  expr_free(Expr *e);

typedef struct ExprCtx {
    const PlayerState  *ps;
    const AppelPhysics  *ap;
    int                  frame;
} ExprCtx;

double expr_eval(const Expr *e, const ExprCtx *ctx);

#endif
