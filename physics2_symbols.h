#ifndef PHYSICS2_SYMBOLS_H
#define PHYSICS2_SYMBOLS_H

#include "physics2_types.h"

#ifdef __cplusplus
extern "C" {
#endif

SymbolId make_symbol(SymbolPool *pool, SymKind kind, StrRef name,
                     Quantity quantity);
const Symbol *symbol_at(const SymbolPool *pool, SymbolId id);
void symbol_pool_free(SymbolPool *pool);

#ifdef __cplusplus
}
#endif

#endif
