#ifndef CODEGEN_H
#define CODEGEN_H

#include <stdint.h>
#include <stddef.h>
#include "ast.h"

typedef struct {
    uint8_t *code;
    size_t len;
    size_t cap;
} ByteBuffer;

uint8_t* codegen_compile(ASTNode *ast, size_t *out_len);
void codegen_free(uint8_t *code);

#endif /* CODEGEN_H */
