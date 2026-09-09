#include "physics2_types.h"

#include <stdlib.h>
#include <string.h>

/* ============================================================
 * Value
 * ============================================================ */

Value value_make(double nominal, double tolerance_pct) {
  Value value;

  value.nominal = nominal;
  value.tolerance_pct = tolerance_pct;

  return value;
}

double value_min(Value value) {
  double delta = value.nominal * value.tolerance_pct / 100.0;

  return value.nominal - delta;
}

double value_max(Value value) {
  double delta = value.nominal * value.tolerance_pct / 100.0;

  return value.nominal + delta;
}

/* ============================================================
 * Dimension algebra
 * ============================================================ */

bool dimension_equal(Dimension a, Dimension b) {
  return memcmp(a.exponents, b.exponents, sizeof(a.exponents)) == 0;
}

Dimension dimension_multiply(Dimension a, Dimension b) {
  Dimension result;

  for (size_t i = 0; i < 7; ++i)
    result.exponents[i] = (int8_t)(a.exponents[i] + b.exponents[i]);

  return result;
}

Dimension dimension_divide(Dimension a, Dimension b) {
  Dimension result;

  for (size_t i = 0; i < 7; ++i)
    result.exponents[i] = (int8_t)(a.exponents[i] - b.exponents[i]);

  return result;
}

bool dimension_is_dimensionless(Dimension dimension) {
  for (size_t i = 0; i < 7; ++i) {
    if (dimension.exponents[i] != 0)
      return false;
  }

  return true;
}

/* ============================================================
 * Quantity
 * ============================================================ */

Quantity quantity_make(QuantityKind kind, Domain domain, Dimension dimension) {
  Quantity quantity;

  quantity.kind = kind;
  quantity.domain = domain;
  quantity.dimension = dimension;

  return quantity;
}

Dimension quantity_dimension(Domain domain, QuantityKind kind) {
  Dimension dimension = {{0}};

  if (domain != DOMAIN_ELECTRICAL)
    return dimension;

  switch (kind) {

  case Q_VOLTAGE:
    /*
     * V = kg*m^2/(s^3*A)
     *
     * [L, M, T, I, Theta, N, J]
     * [2, 1, -3, -1, 0, 0, 0]
     */
    dimension.exponents[0] = 2;
    dimension.exponents[1] = 1;
    dimension.exponents[2] = -3;
    dimension.exponents[3] = -1;
    break;

  case Q_CURRENT:
    dimension.exponents[3] = 1;
    break;

  default:
    break;
  }

  return dimension;
}

Quantity quantity_from_dimension(Domain domain, Dimension dimension) {
  return quantity_make(Q_NONE, domain, dimension);
}

/* ============================================================
 * String arena
 * ============================================================ */

void str_arena_init(StrArena *arena) {
  if (!arena)
    return;

  arena->buffer = NULL;
  arena->count = 0;
  arena->capacity = 0;
}

StrRef str_arena_add(StrArena *arena, const char *str) {
  if (!arena || !str)
    return 0;

  size_t len = strlen(str) + 1;

  if (arena->count + len > arena->capacity) {

    size_t new_capacity = arena->capacity ? arena->capacity : 128;

    while (new_capacity < arena->count + len) {

      if (new_capacity > SIZE_MAX / 2)
        return 0;

      new_capacity *= 2;
    }

    char *buffer = realloc(arena->buffer, new_capacity);

    if (!buffer)
      return 0;

    arena->buffer = buffer;
    arena->capacity = new_capacity;
  }

  StrRef ref = (StrRef)arena->count;

  memcpy(arena->buffer + arena->count, str, len);

  arena->count += len;

  return ref;
}

const char *str_arena_get(const StrArena *arena, StrRef ref) {
  if (!arena || !arena->buffer || ref >= arena->count)
    return NULL;

  return arena->buffer + ref;
}

void str_arena_free(StrArena *arena) {
  if (!arena)
    return;

  free(arena->buffer);

  arena->buffer = NULL;
  arena->count = 0;
  arena->capacity = 0;
}

/* ============================================================
 * Expression pool
 * ============================================================ */

static ExprId expr_push(ExprPool *pool, Expr expr) {
  if (!pool)
    return EXPR_NONE;

  if (pool->count == pool->capacity) {

    size_t capacity = pool->capacity ? pool->capacity * 2 : 16;

    if (capacity > SIZE_MAX / sizeof(*pool->exprs))
      return EXPR_NONE;

    Expr *exprs = realloc(pool->exprs, capacity * sizeof(*pool->exprs));

    if (!exprs)
      return EXPR_NONE;

    pool->exprs = exprs;
    pool->capacity = capacity;
  }

  pool->exprs[pool->count] = expr;

  pool->count++;

  return (ExprId)pool->count - 1;
}

ExprId make_val_expr(ExprPool *pool, double val) {
  Expr expression = {0};

  expression.op = OP_VAL;
  expression.literal = val;

  return expr_push(pool, expression);
}

ExprId make_par_expr(ExprPool *pool, SymbolId sym) {
  Expr expression = {0};

  expression.op = OP_PAR;
  expression.symbol = sym;

  return expr_push(pool, expression);
}

ExprId make_binary_expr(ExprPool *pool, ExprOp op, ExprId left, ExprId right) {
  if (!pool)
    return EXPR_NONE;

  Expr expression = {0};

  expression.op = op;
  expression.left = left;
  expression.right = right;

  return expr_push(pool, expression);
}

ExprId make_unary_expr(ExprPool *pool, ExprOp op, ExprId operand) {
  if (!pool)
    return EXPR_NONE;

  Expr expression = {0};

  expression.op = op;
  expression.left = operand;

  return expr_push(pool, expression);
}

const Expr *expr_at(const ExprPool *pool, ExprId id) {
  if (!pool || id == EXPR_NONE || id >= pool->count)
    return NULL;

  return &pool->exprs[id];
}

void expr_pool_free(ExprPool *pool) {
  if (!pool)
    return;

  free(pool->exprs);

  pool->exprs = NULL;
  pool->count = 0;
  pool->capacity = 0;
}
