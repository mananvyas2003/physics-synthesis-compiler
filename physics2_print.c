#include "physics2_print.h"

#include <stdio.h>

static const Expr *get_expr(const ExprPool *pool, ExprId id) {
  if (!pool || !pool->exprs)
    return NULL;

  if (id == EXPR_NONE || id > pool->count)
    return NULL;

  return &pool->exprs[id - 1];
}

static void print_indent(int depth) {
  for (int i = 0; i < depth; i++)
    printf("  ");
}

static const char *symbol_name(const SymbolPool *symbols,
                               const StrArena *strings, SymbolId id) {
  const Symbol *symbol = symbol_at(symbols, id);

  if (!symbol)
    return "<invalid-symbol>";

  const char *name = str_arena_get(strings, symbol->name);

  if (!name)
    return "<unnamed>";

  return name;
}

static void print_expr_internal(const ExprPool *expr_pool,
                                const SymbolPool *symbol_pool,
                                const StrArena *strings, ExprId id) {
  const Expr *expr = get_expr(expr_pool, id);

  if (!expr) {
    printf("<invalid>");
    return;
  }

  switch (expr->op) {

  case OP_NONE:
    printf("<none>");
    break;

  case OP_VAL:
    printf("%g", expr->literal);
    break;

  case OP_PAR:
    printf("%s", symbol_name(symbol_pool, strings, expr->symbol));
    break;

  case OP_ADD:
    printf("(");

    print_expr_internal(expr_pool, symbol_pool, strings, expr->left);

    printf(" + ");

    print_expr_internal(expr_pool, symbol_pool, strings, expr->right);

    printf(")");
    break;

  case OP_SUB:
    printf("(");

    print_expr_internal(expr_pool, symbol_pool, strings, expr->left);

    printf(" - ");

    print_expr_internal(expr_pool, symbol_pool, strings, expr->right);

    printf(")");
    break;

  case OP_MUL:
    printf("(");

    print_expr_internal(expr_pool, symbol_pool, strings, expr->left);

    printf(" * ");

    print_expr_internal(expr_pool, symbol_pool, strings, expr->right);

    printf(")");
    break;

  case OP_DIV:
    printf("(");

    print_expr_internal(expr_pool, symbol_pool, strings, expr->left);

    printf(" / ");

    print_expr_internal(expr_pool, symbol_pool, strings, expr->right);

    printf(")");
    break;

  case OP_EXP:
    printf("exp(");

    print_expr_internal(expr_pool, symbol_pool, strings, expr->left);

    printf(")");
    break;

  case OP_POW:
    printf("(");

    print_expr_internal(expr_pool, symbol_pool, strings, expr->left);

    printf(" ^ ");

    print_expr_internal(expr_pool, symbol_pool, strings, expr->right);

    printf(")");
    break;

  default:
    printf("<unknown-op>");
    break;
  }
}

void print_expr(const ExprPool *expr_pool, const SymbolPool *symbol_pool,
                const StrArena *strings, ExprId id) {
  print_expr_internal(expr_pool, symbol_pool, strings, id);

  printf("\n");
}

static void print_tree_internal(const ExprPool *expr_pool,
                                const SymbolPool *symbol_pool,
                                const StrArena *strings, ExprId id, int depth) {
  const Expr *expr = get_expr(expr_pool, id);

  if (!expr) {
    print_indent(depth);
    printf("<invalid>\n");
    return;
  }

  print_indent(depth);

  switch (expr->op) {

  case OP_NONE:
    printf("NONE\n");
    break;

  case OP_VAL:
    printf("VAL %g\n", expr->literal);
    break;

  case OP_PAR:
    printf("PAR %s [symbol=%u]\n",
           symbol_name(symbol_pool, strings, expr->symbol),
           (unsigned)expr->symbol);
    break;

  case OP_ADD:
    printf("ADD [expr=%u]\n", (unsigned)id);

    print_tree_internal(expr_pool, symbol_pool, strings, expr->left, depth + 1);

    print_tree_internal(expr_pool, symbol_pool, strings, expr->right,
                        depth + 1);
    break;

  case OP_SUB:
    printf("SUB [expr=%u]\n", (unsigned)id);

    print_tree_internal(expr_pool, symbol_pool, strings, expr->left, depth + 1);

    print_tree_internal(expr_pool, symbol_pool, strings, expr->right,
                        depth + 1);
    break;

  case OP_MUL:
    printf("MUL [expr=%u]\n", (unsigned)id);

    print_tree_internal(expr_pool, symbol_pool, strings, expr->left, depth + 1);

    print_tree_internal(expr_pool, symbol_pool, strings, expr->right,
                        depth + 1);
    break;

  case OP_DIV:
    printf("DIV [expr=%u]\n", (unsigned)id);

    print_tree_internal(expr_pool, symbol_pool, strings, expr->left, depth + 1);

    print_tree_internal(expr_pool, symbol_pool, strings, expr->right,
                        depth + 1);
    break;

  case OP_EXP:
    printf("EXP [expr=%u]\n", (unsigned)id);

    print_tree_internal(expr_pool, symbol_pool, strings, expr->left, depth + 1);
    break;

  case OP_POW:
    printf("POW [expr=%u]\n", (unsigned)id);

    print_tree_internal(expr_pool, symbol_pool, strings, expr->left, depth + 1);

    print_tree_internal(expr_pool, symbol_pool, strings, expr->right,
                        depth + 1);
    break;

  default:
    printf("UNKNOWN [expr=%u]\n", (unsigned)id);
    break;
  }
}

void print_expr_tree(const ExprPool *expr_pool, const SymbolPool *symbol_pool,
                     const StrArena *strings, ExprId id) {
  print_tree_internal(expr_pool, symbol_pool, strings, id, 0);
}
