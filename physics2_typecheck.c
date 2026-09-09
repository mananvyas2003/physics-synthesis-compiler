#include "physics2_typecheck.h"

#include <limits.h>
#include <math.h>

static TypeResult type_error(TypeStatus status) {
  TypeResult result;

  result.status = status;
  result.quantity = quantity_from_dimension(DOMAIN_NONE, (Dimension){{0}});

  return result;
}

static TypeResult type_ok(Quantity quantity) {
  TypeResult result;

  result.status = TYPE_OK;
  result.quantity = quantity;

  return result;
}

static bool is_valid_expr(const ExprPool *pool, ExprId id) {
  return pool != NULL && id != EXPR_NONE && id <= pool->count &&
         pool->exprs != NULL;
}

static bool is_integer_double(double value) {
  if (!isfinite(value))
    return false;

  return trunc(value) == value;
}

static bool multiply_dimension_by_integer(Dimension input, long long factor,
                                          Dimension *output) {
  if (!output)
    return false;

  for (int i = 0; i < 7; i++) {
    long long value = (long long)input.exponents[i] * factor;

    if (value < INT8_MIN || value > INT8_MAX)
      return false;

    output->exponents[i] = (int8_t)value;
  }

  return true;
}

static TypeResult resolve_expression_type(const ExprPool *expr_pool,
                                          const SymbolPool *symbol_pool,
                                          ExprId id) {
  if (!is_valid_expr(expr_pool, id))
    return type_error(TYPE_INVALID_EXPR);

  const Expr *expr = &expr_pool->exprs[id - 1];

  switch (expr->op) {

  case OP_NONE:
    return type_error(TYPE_INVALID_EXPR);

  case OP_VAL:
    return type_ok(quantity_from_dimension(DOMAIN_NONE, (Dimension){{0}}));

  case OP_PAR: {
    const Symbol *symbol = symbol_at(symbol_pool, expr->symbol);

    if (!symbol)
      return type_error(TYPE_INVALID_SYMBOL);

    return type_ok(symbol->quantity);
  }

  case OP_ADD:
  case OP_SUB: {
    TypeResult left =
        resolve_expression_type(expr_pool, symbol_pool, expr->left);

    if (left.status != TYPE_OK)
      return left;

    TypeResult right =
        resolve_expression_type(expr_pool, symbol_pool, expr->right);

    if (right.status != TYPE_OK)
      return right;

    if (!dimension_equal(left.quantity.dimension, right.quantity.dimension)) {
      return type_error(TYPE_DIMENSION_MISMATCH);
    }

    return type_ok(left.quantity);
  }

  case OP_MUL: {
    TypeResult left =
        resolve_expression_type(expr_pool, symbol_pool, expr->left);

    if (left.status != TYPE_OK)
      return left;

    TypeResult right =
        resolve_expression_type(expr_pool, symbol_pool, expr->right);

    if (right.status != TYPE_OK)
      return right;

    Dimension dimension =
        dimension_multiply(left.quantity.dimension, right.quantity.dimension);

    return type_ok(quantity_from_dimension(DOMAIN_NONE, dimension));
  }

  case OP_DIV: {
    TypeResult left =
        resolve_expression_type(expr_pool, symbol_pool, expr->left);

    if (left.status != TYPE_OK)
      return left;

    TypeResult right =
        resolve_expression_type(expr_pool, symbol_pool, expr->right);

    if (right.status != TYPE_OK)
      return right;

    Dimension dimension =
        dimension_divide(left.quantity.dimension, right.quantity.dimension);

    return type_ok(quantity_from_dimension(DOMAIN_NONE, dimension));
  }

  case OP_EXP: {
    TypeResult operand =
        resolve_expression_type(expr_pool, symbol_pool, expr->left);

    if (operand.status != TYPE_OK)
      return operand;

    if (!dimension_is_dimensionless(operand.quantity.dimension)) {
      return type_error(TYPE_EXP_REQUIRES_DIMENSIONLESS);
    }

    return type_ok(quantity_from_dimension(DOMAIN_NONE, (Dimension){{0}}));
  }

  case OP_POW: {
    TypeResult base =
        resolve_expression_type(expr_pool, symbol_pool, expr->left);

    if (base.status != TYPE_OK)
      return base;

    if (!is_valid_expr(expr_pool, expr->right)) {
      return type_error(TYPE_INVALID_EXPR);
    }

    const Expr *exponent = &expr_pool->exprs[expr->right - 1];

    if (exponent->op != OP_VAL) {
      return type_error(TYPE_POW_REQUIRES_CONSTANT_EXPONENT);
    }

    Quantity exponent_quantity =
        quantity_from_dimension(DOMAIN_NONE, (Dimension){{0}});

    if (!dimension_is_dimensionless(exponent_quantity.dimension)) {
      return type_error(TYPE_POW_REQUIRES_DIMENSIONLESS_EXPONENT);
    }

    double exponent_value = exponent->literal;

    Dimension result_dimension = base.quantity.dimension;

    if (!dimension_is_dimensionless(base.quantity.dimension)) {

      if (!is_integer_double(exponent_value))
        return type_error(TYPE_POW_REQUIRES_INTEGER_DIMENSIONAL_EXPONENT);

      if (exponent_value < INT_MIN || exponent_value > INT_MAX) {
        return type_error(TYPE_POW_DIMENSION_OVERFLOW);
      }

      long long integer_exponent = (long long)exponent_value;

      if (!multiply_dimension_by_integer(base.quantity.dimension,
                                         integer_exponent, &result_dimension)) {
        return type_error(TYPE_POW_DIMENSION_OVERFLOW);
      }
    }

    return type_ok(quantity_from_dimension(DOMAIN_NONE, result_dimension));
  }

  default:
    return type_error(TYPE_INVALID_EXPR);
  }
}

TypeResult expr_type(const ExprPool *expr_pool, const SymbolPool *symbol_pool,
                     ExprId id) {
  return resolve_expression_type(expr_pool, symbol_pool, id);
}

bool expr_is_valid(const ExprPool *expr_pool, const SymbolPool *symbol_pool,
                   ExprId id) {
  TypeResult result = expr_type(expr_pool, symbol_pool, id);

  return result.status == TYPE_OK;
}

const char *type_status_name(TypeStatus status) {
  switch (status) {
  case TYPE_OK:
    return "OK";

  case TYPE_INVALID_EXPR:
    return "INVALID_EXPR";

  case TYPE_INVALID_SYMBOL:
    return "INVALID_SYMBOL";

  case TYPE_DIMENSION_MISMATCH:
    return "DIMENSION_MISMATCH";

  case TYPE_EXP_REQUIRES_DIMENSIONLESS:
    return "EXP_REQUIRES_DIMENSIONLESS";

  case TYPE_POW_REQUIRES_DIMENSIONLESS_EXPONENT:
    return "POW_REQUIRES_DIMENSIONLESS_EXPONENT";

  case TYPE_POW_REQUIRES_CONSTANT_EXPONENT:
    return "POW_REQUIRES_CONSTANT_EXPONENT";

  case TYPE_POW_REQUIRES_INTEGER_DIMENSIONAL_EXPONENT:
    return "POW_REQUIRES_INTEGER_DIMENSIONAL_EXPONENT";

  case TYPE_POW_DIMENSION_OVERFLOW:
    return "POW_DIMENSION_OVERFLOW";

  default:
    return "UNKNOWN";
  }
}
