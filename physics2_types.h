#ifndef PHYSICS2_TYPES_H
#define PHYSICS2_TYPES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
 * Sentinel IDs
 * ============================================================ */

#define SYMBOL_NONE UINT32_MAX
#define EXPR_NONE UINT32_MAX
#define NODE_NONE UINT32_MAX
#define PIN_NONE UINT32_MAX

/* ============================================================
 * Physical domains
 * ============================================================ */

typedef enum Domain {
  DOMAIN_NONE = 0,
  DOMAIN_ELECTRICAL,
  DOMAIN_MECHANICAL
} Domain;

/* ============================================================
 * Quantity kinds
 * ============================================================ */

typedef enum QuantityKind { Q_NONE = 0, Q_VOLTAGE, Q_CURRENT } QuantityKind;

/* ============================================================
 * SI base dimensions
 *
 * [L, M, T, I, Theta, N, J]
 * ============================================================ */

typedef struct Dimension {
  int8_t exponents[7];
} Dimension;

/* ============================================================
 * Quantity
 * ============================================================ */

typedef struct Quantity {
  QuantityKind kind;
  Domain domain;
  Dimension dimension;
} Quantity;

/* ============================================================
 * Value
 *
 * Physical component values are not bare doubles.
 * Nominal + tolerance gives deterministic min/max bounds.
 * ============================================================ */

typedef struct Value {
  double nominal;
  double tolerance_pct;
} Value;

double value_min(Value value);
double value_max(Value value);

/* ============================================================
 * String arena
 * ============================================================ */

typedef uint32_t StrRef;

typedef struct StrArena {
  char *buffer;
  size_t count;
  size_t capacity;
} StrArena;

/* ============================================================
 * Symbol system
 * ============================================================ */

typedef uint32_t SymbolId;

typedef enum SymKind { SYM_NONE = 0, SYM_PAR } SymKind;

typedef struct Symbol {
  SymbolId id;
  SymKind kind;
  StrRef name;
  Quantity quantity;
} Symbol;

typedef struct SymbolPool {
  Symbol *symbols;
  size_t count;
  size_t capacity;
} SymbolPool;

/* ============================================================
 * Expression system
 *
 * Kept intact for current dependent code.
 * Primitive physics will eventually consume expressions
 * and/or residuals through the new kernel interfaces.
 * ============================================================ */

typedef enum ExprOp {
  OP_NONE = 0,
  OP_VAL,
  OP_PAR,
  OP_ADD,
  OP_SUB,
  OP_MUL,
  OP_DIV,
  OP_EXP,
  OP_POW
} ExprOp;

typedef uint32_t ExprId;

typedef struct Expr {
  ExprOp op;
  double literal;
  SymbolId symbol;
  ExprId left;
  ExprId right;
} Expr;

typedef struct ExprPool {
  Expr *exprs;
  size_t count;
  size_t capacity;
} ExprPool;

/* ============================================================
 * Primitive system
 * ============================================================ */

/*
 * PrimitiveKind is diagnostic metadata.
 * PrimitiveOps is the actual behavioral interface.
 */
typedef enum PrimitiveKind {
  PRIMITIVE_NONE = 0,

  PRIMITIVE_RESISTOR,
  PRIMITIVE_CAPACITOR,
  PRIMITIVE_INDUCTOR,
  PRIMITIVE_VSOURCE,
  PRIMITIVE_ISOURCE,

  PRIMITIVE_VCVS,
  PRIMITIVE_VCCS,
  PRIMITIVE_CCVS,
  PRIMITIVE_CCCS,

  PRIMITIVE_DIODE,
  PRIMITIVE_TRANSISTOR,

  PRIMITIVE_LOGIC_GATE
} PrimitiveKind;

/* ============================================================
 * Physical primitive value payloads
 * ============================================================ */

typedef struct TwoTerminal {
  Value value;
} TwoTerminal;

/* Controlled sources:
 *
 * terminals:
 *   0,1 = output
 *   2,3 = control
 */
typedef struct ControlledSource {
  Value gain;
} ControlledSource;

/* ============================================================
 * Logic gate payload
 * ============================================================ */

typedef struct LogicGate {
  uint8_t input_count;
  uint8_t output_count;

  /*
   * Truth table storage is intentionally opaque for now.
   * The primitive ABI only promises that a LogicGate exists;
   * the concrete encoding comes when the digital execution
   * model is defined.
   */
  uint8_t *truth_table;
  size_t truth_table_size;
} LogicGate;

/* ============================================================
 * Primitive
 * ============================================================ */

typedef struct Primitive Primitive;
typedef struct PrimitiveOps PrimitiveOps;

/*
 * Forward declaration only.
 *
 * Primitive implementations must not know whether the solver
 * uses dense, sparse, block-sparse, GPU, etc. storage.
 */
typedef struct PhysicsAccumulator PhysicsAccumulator;

/* ============================================================
 * Primitive operations
 * ============================================================ */

struct PrimitiveOps {

  /*
   * Assemble this primitive's contribution into the current
   * analysis accumulator.
   */
  bool (*stamp)(const Primitive *primitive, PhysicsAccumulator *accumulator);

  /*
   * Number of terminals exposed by this primitive.
   */
  uint8_t (*terminal_count)(const Primitive *primitive);

  /*
   * Human-readable diagnostic output.
   */
  void (*print)(const Primitive *primitive, FILE *stream);
};

/* ============================================================
 * Primitive payload union
 * ============================================================ */

typedef union PrimitivePayload {

  TwoTerminal two_terminal;
  ControlledSource controlled_source;
  LogicGate logic_gate;

} PrimitivePayload;

/* ============================================================
 * Primitive object
 *
 * ops = behavioral type
 * kind = diagnostic identity
 * payload = concrete instance data
 * ============================================================ */

struct Primitive {

  const PrimitiveOps *ops;

  PrimitiveKind kind;

  char name[64];

  PrimitivePayload payload;
};

/* ============================================================
 * Pin / Net IDs
 * ============================================================ */

typedef uint32_t NodeId;
typedef uint32_t PinId;

/* ============================================================
 * Value constructor
 * ============================================================ */

Value value_make(double nominal, double tolerance_pct);

/* ============================================================
 * Dimension algebra
 * ============================================================ */

bool dimension_equal(Dimension a, Dimension b);

Dimension dimension_multiply(Dimension a, Dimension b);

Dimension dimension_divide(Dimension a, Dimension b);

bool dimension_is_dimensionless(Dimension d);

/* ============================================================
 * Quantity constructors
 * ============================================================ */

Quantity quantity_make(QuantityKind kind, Domain domain, Dimension dimension);

Dimension quantity_dimension(Domain domain, QuantityKind kind);

Quantity quantity_from_dimension(Domain domain, Dimension dimension);

/* ============================================================
 * String arena
 * ============================================================ */

void str_arena_init(StrArena *arena);

StrRef str_arena_add(StrArena *arena, const char *str);

const char *str_arena_get(const StrArena *arena, StrRef ref);

void str_arena_free(StrArena *arena);

/* ============================================================
 * Symbol pool
 * ============================================================ */

void symbol_pool_free(SymbolPool *pool);

SymbolId make_symbol(SymbolPool *pool, SymKind kind, StrRef name,
                     Quantity quantity);

const Symbol *symbol_at(const SymbolPool *pool, SymbolId id);

/* ============================================================
 * Expression pool
 * ============================================================ */

ExprId make_val_expr(ExprPool *pool, double val);

ExprId make_par_expr(ExprPool *pool, SymbolId sym);

ExprId make_binary_expr(ExprPool *pool, ExprOp op, ExprId left, ExprId right);

ExprId make_unary_expr(ExprPool *pool, ExprOp op, ExprId operand);

const Expr *expr_at(const ExprPool *pool, ExprId id);

void expr_pool_free(ExprPool *pool);

#ifdef __cplusplus
}
#endif

#endif
