/* Minimal freestanding <stdlib.h> for the WebAssembly build (implemented in ../libc.c). */
#ifndef TCG_WASM_STDLIB_H
#define TCG_WASM_STDLIB_H
#include <stddef.h>
_Noreturn void abort(void);
int abs(int v);
#endif
