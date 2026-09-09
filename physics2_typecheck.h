#ifndef PHYSICS2_TYPECHECK_H
#define PHYSICS2_TYPECHECK_H

#include "physics2_types.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum TypeStatus {
  TYPE_OK = 0,
  TYPE_INVALID_EXPR,
  TYPE_INVALID_SYMBOL,
  TYPE_DIMENSION_MISMATCH,
  TYPE_EXP_REQUIRES_DIMENSIONLESS,
  TYPE_POW_REQUIRES_DIMENSIONLESS_EXPONENT,
  TYPE_POW_REQUIRES_CONSTANT_EXPONENT,
  TYPE_POW_REQUIRES_INTEGER_DIMENSIONAL_EXPONENT,
  TYPE_POW_DIMENSION_OVERFLOW
} TypeStatus;

typedef struct TypeResult {
  TypeStatus status;
  Quantity quantity;
} TypeResult;

TypeResult expr_type(const ExprPool *expr_pool, const SymbolPool *symbol_pool,
                     ExprId id);

bool expr_is_valid(const ExprPool *expr_pool, const SymbolPool *symbol_pool,
                   ExprId id);

const char *type_status_name(TypeStatus status);

#ifdef __cplusplus
}
#endif

#endif
