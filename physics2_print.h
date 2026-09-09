#ifndef PHYSICS2_PRINT_H
#define PHYSICS2_PRINT_H

#include "physics2_types.h"

#ifdef __cplusplus
extern "C" {
#endif

void print_expr(const ExprPool *expr_pool, const SymbolPool *symbol_pool,
                const StrArena *strings, ExprId id);

void print_expr_tree(const ExprPool *expr_pool, const SymbolPool *symbol_pool,
                     const StrArena *strings, ExprId id);

#ifdef __cplusplus
}
#endif

#endif
