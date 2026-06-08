#include "vm.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define STACK_SIZE_DEFAULT 1024

struct VarEntry {
    char *name;
    uint64_t value;
    struct VarEntry *next;
};

typedef struct VarEntry VarEntry;

typedef struct {
    VarEntry *entries;
    size_t size;
} VarTable;

static VarTable* vartable_create(size_t size) {
    VarTable *t = malloc(sizeof(VarTable));
    if (!t) return NULL;
    t->entries = calloc(size, sizeof(VarEntry));
    t->size = size;
    return t;
}

static void vartable_destroy(VarTable *t) {
    if (!t) return;
    for (size_t i = 0; i < t->size; i++) {
        VarEntry *entry = t->entries + i;
        if (entry->name) {
            free(entry->name);
            entry->name = NULL;
        }
        VarEntry *curr = entry->next;
        while (curr) {
            VarEntry *next = curr->next;
            free(curr->name);
            free(curr);
            curr = next;
        }
    }
    free(t->entries);
    free(t);
}

static unsigned long hash_string(const char *str) {
    unsigned long h = 5381;
    int c;
    while ((c = *str++)) {
        h = ((h << 5) + h) + c;
    }
    return h;
}

static void vartable_set(VarTable *t, const char *name, uint64_t value) {
    if (!t || !name) return;
    
    unsigned long h = hash_string(name) % t->size;
    VarEntry *entry = t->entries + h;
    
    if (!entry->name) {
        entry->name = malloc(strlen(name) + 1);
        strcpy(entry->name, name);
        entry->value = value;
        return;
    }
    
    VarEntry *curr = entry;
    while (curr) {
        if (strcmp(curr->name, name) == 0) {
            curr->value = value;
            return;
        }
        if (!curr->next) break;
        curr = curr->next;
    }
    
    VarEntry *new_entry = malloc(sizeof(VarEntry));
    new_entry->name = malloc(strlen(name) + 1);
    strcpy(new_entry->name, name);
    new_entry->value = value;
    new_entry->next = NULL;
    curr->next = new_entry;
}

static uint64_t vartable_get(VarTable *t, const char *name) {
    if (!t || !name) return 0;
    
    unsigned long h = hash_string(name) % t->size;
    VarEntry *entry = t->entries + h;
    
    VarEntry *curr = entry;
    while (curr) {
        if (curr->name && strcmp(curr->name, name) == 0) {
            return curr->value;
        }
        curr = curr->next;
    }
    
    return 0;
}

VM* vm_create(uint8_t *bytecode, size_t len, size_t stack_size) {
    if (stack_size == 0) stack_size = STACK_SIZE_DEFAULT;
    
    VM *vm = malloc(sizeof(VM));
    if (!vm) return NULL;
    
    vm->stack = malloc(stack_size * sizeof(uint64_t));
    if (!vm->stack) {
        free(vm);
        return NULL;
    }
    
    vm->code = bytecode;
    vm->code_len = len;
    vm->code_ptr = 0;
    vm->stack_ptr = 0;
    vm->stack_size = stack_size;
    
    vm->variables = vartable_create(256);
    if (!vm->variables) {
        free(vm->stack);
        free(vm);
        return NULL;
    }
    
    return vm;
}

void vm_destroy(VM *vm) {
    if (!vm) return;
    if (vm->stack) free(vm->stack);
    if (vm->variables) vartable_destroy((VarTable*)vm->variables);
    free(vm);
}

VMStatus vm_set_variable(VM *vm, const char *name, uint64_t value) {
    if (!vm || !name) return VM_ERROR_EXEC;
    vartable_set((VarTable*)vm->variables, name, value);
    return VM_OK;
}

uint64_t vm_get_variable(VM *vm, const char *name) {
    if (!vm || !name) return 0;
    return vartable_get((VarTable*)vm->variables, name);
}

static inline void push_stack(VM *vm, uint64_t value) {
    if (vm->stack_ptr < vm->stack_size) {
        vm->stack[vm->stack_ptr++] = value;
    }
}

static inline uint64_t pop_stack(VM *vm) {
    if (vm->stack_ptr > 0) {
        return vm->stack[--vm->stack_ptr];
    }
    return 0;
}

static inline uint64_t peek_stack(VM *vm) {
    if (vm->stack_ptr > 0) {
        return vm->stack[vm->stack_ptr - 1];
    }
    return 0;
}

static inline uint8_t next_byte(VM *vm) {
    if (vm->code_ptr < vm->code_len) {
        return vm->code[vm->code_ptr++];
    }
    return 0xFF; /* HALT */
}

static inline uint64_t next_int64(VM *vm) {
    uint64_t val = 0;
    if (vm->code_ptr + 8 <= vm->code_len) {
        memcpy(&val, vm->code + vm->code_ptr, 8);
        vm->code_ptr += 8;
    }
    return val;
}

static inline uint32_t next_uint32(VM *vm) {
    uint32_t val = 0;
    if (vm->code_ptr + 4 <= vm->code_len) {
        memcpy(&val, vm->code + vm->code_ptr, 4);
        vm->code_ptr += 4;
    }
    return val;
}

static inline uint16_t next_uint16(VM *vm) {
    uint16_t val = 0;
    if (vm->code_ptr + 2 <= vm->code_len) {
        memcpy(&val, vm->code + vm->code_ptr, 2);
        vm->code_ptr += 2;
    }
    return val;
}

static char* read_string(VM *vm) {
    uint32_t len = next_uint32(vm);
    if (vm->code_ptr + len > vm->code_len) return NULL;
    
    char *str = malloc(len + 1);
    if (!str) return NULL;
    
    memcpy(str, vm->code + vm->code_ptr, len);
    str[len] = '\0';
    vm->code_ptr += len;
    
    return str;
}

static char* read_ident(VM *vm) {
    uint16_t len = next_uint16(vm);
    if (vm->code_ptr + len > vm->code_len) return NULL;
    
    char *str = malloc(len + 1);
    if (!str) return NULL;
    
    memcpy(str, vm->code + vm->code_ptr, len);
    str[len] = '\0';
    vm->code_ptr += len;
    
    return str;
}

VMStatus vm_execute(VM *vm) {
    if (!vm || !vm->code) return VM_ERROR_EXEC;
    
    while (vm->code_ptr < vm->code_len) {
        uint8_t opcode = next_byte(vm);
        
        switch (opcode) {
            case 0x01: {  /* LOAD_NUM */
                uint64_t val = next_int64(vm);
                push_stack(vm, val);
                break;
            }
            
            case 0x02: {  /* LOAD_STR */
                char *str = read_string(vm);
                if (str) {
                    printf("%s", str);
                    free(str);
                }
                break;
            }
            
            case 0x03: {  /* LOAD_VAR */
                char *name = read_ident(vm);
                if (name) {
                    uint64_t val = vm_get_variable(vm, name);
                    push_stack(vm, val);
                    free(name);
                }
                break;
            }
            
            case 0x04: {  /* STORE_VAR */
                char *name = read_ident(vm);
                if (name) {
                    uint64_t val = pop_stack(vm);
                    vm_set_variable(vm, name, val);
                    free(name);
                }
                break;
            }
            
            case 0x10: {  /* ADD */
                uint64_t b = pop_stack(vm);
                uint64_t a = pop_stack(vm);
                push_stack(vm, a + b);
                break;
            }
            
            case 0x11: {  /* SUB */
                uint64_t b = pop_stack(vm);
                uint64_t a = pop_stack(vm);
                push_stack(vm, a - b);
                break;
            }
            
            case 0x12: {  /* MUL */
                uint64_t b = pop_stack(vm);
                uint64_t a = pop_stack(vm);
                push_stack(vm, a * b);
                break;
            }
            
            case 0x13: {  /* DIV */
                uint64_t b = pop_stack(vm);
                uint64_t a = pop_stack(vm);
                if (b == 0) return VM_ERROR_DIV_ZERO;
                push_stack(vm, a / b);
                break;
            }
            
            case 0x14: {  /* MOD */
                uint64_t b = pop_stack(vm);
                uint64_t a = pop_stack(vm);
                if (b == 0) return VM_ERROR_DIV_ZERO;
                push_stack(vm, a % b);
                break;
            }
            
            case 0x15: {  /* LT */
                uint64_t b = pop_stack(vm);
                uint64_t a = pop_stack(vm);
                push_stack(vm, a < b ? 1 : 0);
                break;
            }
            
            case 0x16: {  /* GT */
                uint64_t b = pop_stack(vm);
                uint64_t a = pop_stack(vm);
                push_stack(vm, a > b ? 1 : 0);
                break;
            }
            
            case 0x17: {  /* EQ */
                uint64_t b = pop_stack(vm);
                uint64_t a = pop_stack(vm);
                push_stack(vm, a == b ? 1 : 0);
                break;
            }
            
            case 0x18: {  /* NEQ */
                uint64_t b = pop_stack(vm);
                uint64_t a = pop_stack(vm);
                push_stack(vm, a != b ? 1 : 0);
                break;
            }
            
            case 0x19: {  /* AND */
                uint64_t b = pop_stack(vm);
                uint64_t a = pop_stack(vm);
                push_stack(vm, a && b ? 1 : 0);
                break;
            }
            
            case 0x1A: {  /* OR */
                uint64_t b = pop_stack(vm);
                uint64_t a = pop_stack(vm);
                push_stack(vm, a || b ? 1 : 0);
                break;
            }
            
            case 0x1B: {  /* NEG */
                uint64_t a = pop_stack(vm);
                push_stack(vm, (uint64_t)(-(int64_t)a));
                break;
            }
            
            case 0x1C: {  /* NOT */
                uint64_t a = pop_stack(vm);
                push_stack(vm, a ? 0 : 1);
                break;
            }
            
            case 0x1D: {  /* BITNOT */
                uint64_t a = pop_stack(vm);
                push_stack(vm, ~a);
                break;
            }
            
            case 0x20: {  /* SAY */
                uint64_t val = pop_stack(vm);
                printf("%lu", val);
                break;
            }
            
            case 0x21: {  /* FEEL */
                uint32_t intensity = next_uint32(vm);
                printf("[FEEL:%u]", intensity);
                break;
            }
            
            case 0x50: {  /* RET */
                return VM_OK;
            }
            
            case 0x60: {  /* JIF */
                uint32_t target = next_uint32(vm);
                uint64_t cond = pop_stack(vm);
                if (!cond) {
                    vm->code_ptr = target;
                }
                break;
            }
            
            case 0x61: {  /* JMP */
                uint32_t target = next_uint32(vm);
                vm->code_ptr = target;
                break;
            }
            
            case 0xFF: {  /* HALT */
                return VM_OK;
            }
            
            default:
                fprintf(stderr, "Unknown opcode: 0x%02x\n", opcode);
                return VM_ERROR_EXEC;
        }
    }
    
    return VM_OK;
}
