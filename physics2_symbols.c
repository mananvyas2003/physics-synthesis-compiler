#include "physics2_types.h"

#include <stdlib.h>

const Symbol *symbol_at(const SymbolPool *pool, SymbolId id) {
  if (!pool || id == SYMBOL_NONE)
    return NULL;

  if (id > pool->count)
    return NULL;

  return &pool->symbols[id - 1];
}

static SymbolId symbol_push(SymbolPool *pool, Symbol symbol) {
  if (!pool)
    return SYMBOL_NONE;

  if (pool->count == pool->capacity) {
    size_t capacity = pool->capacity ? pool->capacity * 2 : 16;

    if (capacity > SIZE_MAX / sizeof(*pool->symbols))
      return SYMBOL_NONE;

    Symbol *symbols = realloc(pool->symbols, capacity * sizeof(*pool->symbols));

    if (!symbols)
      return SYMBOL_NONE;

    pool->symbols = symbols;
    pool->capacity = capacity;
  }

  SymbolId id = (SymbolId)(pool->count + 1);

  symbol.id = id;

  pool->symbols[pool->count] = symbol;
  pool->count++;

  return id;
}

SymbolId make_symbol(SymbolPool *pool, SymKind kind, StrRef name,
                     Quantity quantity) {
  Symbol symbol = {0};

  symbol.kind = kind;
  symbol.name = name;
  symbol.quantity = quantity;

  return symbol_push(pool, symbol);
}

void symbol_pool_free(SymbolPool *pool) {
  if (!pool)
    return;

  free(pool->symbols);

  pool->symbols = NULL;
  pool->count = 0;
  pool->capacity = 0;
}
