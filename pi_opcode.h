#ifndef PI_OPCODE_H
#define PI_OPCODE_H

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

// Define the OpCode enum with OP_ prefix
typedef enum
{
    OP_RETURN = 0x4,
    OP_LOAD_CONST = 0x5,
    OP_PRINT = 0x6,
    OP_POP = 0x7,
    OP_PUSH = 0x8,
    OP_PUSH_NIL = 0x9,
    OP_STORE_GLOBAL = 0xa,
    OP_LOAD_GLOBAL = 0xb,
    OP_STORE_LOCAL = 0xc,
    OP_LOAD_LOCAL = 0xd,
    OP_JUMP = 0xe,
    OP_JUMP_IF_FALSE = 0xf,
    OP_CALL_FUNCTION = 0x11,
    OP_CALL_FUNCTION_KW = 0x12,
    OP_POP_N = 0x13,
    OP_COMPARE = 0x14,
    OP_JUMP_IF_TRUE = 0x15,
    OP_HALT = 0x16,
    OP_PUSH_ITER = 0x17,
    OP_LOOP = 0x18,
    OP_PUSH_RANGE = 0x19,
    OP_BINARY = 0x1a,
    OP_PUSH_LIST = 0x1b,
    OP_LIST_APPEND = 0x20,
    OP_LIST_EXTEND = 0x21,
    OP_STORE_UPVALUE = 0x1c,
    OP_LOAD_UPVALUE = 0x1d,
    OP_NO = 0x1e,
    OP_CREATE_UPVALUE = 0x1f,
    OP_PUSH_FUNCTION = 0x22,
    OP_PUSH_MAP = 0x23,
    OP_PUSH_UPVALUE = 0x24,
    OP_PUSH_CLOSURE = 0x25,
    OP_DUP_TOP = 0x26,
    OP_PUSH_SLICE = 0x27,
    OP_GET_ITEM = 0x28,
    OP_SET_ITEM = 0x29,
    OP_UNARY = 0x2a,
    OP_DEBUG = 0x2b,
    OP_POP_ITER = 0x2c,
    OP_CLOSE_UPVALUE = 0x3c,
    OP_IMPORT = 0x3d,
    OP_GET_EXPORT = 0x3e,
    OP_IMPORT_ALL = 0x3f,
    OP_IMPORT_DEFAULT = 0x40,
    OP_LOAD_SUPER = 0x41,
    OP_MAT_GET = 0x42,
    OP_MAT_SET = 0x43,
    OP_LIST_FINALIZE = 0x44,
    OP_CALL_SPREAD = 0x45,
    OP_MAP_SET = 0x46,
    OP_MAP_EXTEND = 0x47,
    OP_MAP_FINALIZE = 0x48,
    OP_COMP_APPEND = 0x49,
    OP_PUSH_SET = 0x4a,
    OP_PUSH_TUPLE = 0x4b,

} OpCode;

typedef struct
{
    OpCode op;
    int value;
    char *name;
    bool has_ops;
    int num_ops;
} opcode_t;

/**
 * Returns the number of operands required by the given opcode.
 *
 * This function takes an opcode as input and returns the number of operands
 * required by the opcode. It uses a switch statement to determine the number
 * of operands based on the opcode.
 *
 * @param op The opcode to determine the number of operands for.
 * @return The number of operands required by the opcode.
 */
static inline int operand_count(uint8_t op)
{
    switch ((OpCode)op)
    {
    case OP_LOAD_CONST:
    case OP_JUMP_IF_FALSE:
    case OP_JUMP:
    case OP_JUMP_IF_TRUE:
    case OP_LOOP:
    case OP_PUSH_LIST:
    case OP_PUSH_MAP:
    case OP_PUSH_SET:
    case OP_PUSH_CLOSURE:
        return 2;

    case OP_STORE_GLOBAL:
    case OP_LOAD_GLOBAL:
    case OP_LOAD_LOCAL:
    case OP_STORE_LOCAL:
    case OP_CALL_FUNCTION:
    case OP_CALL_FUNCTION_KW:
    case OP_POP_N:
    case OP_COMPARE:
    case OP_BINARY:
    case OP_STORE_UPVALUE:
    case OP_LOAD_UPVALUE:
    case OP_CREATE_UPVALUE:
    case OP_PUSH_FUNCTION:
    case OP_PUSH_UPVALUE:
    case OP_UNARY:
    case OP_MAT_GET:
    case OP_MAT_SET:
    case OP_CALL_SPREAD:
    case OP_COMP_APPEND:
    case OP_MAP_FINALIZE:
        return 1;

    default:
        return 0;
    }
}

#endif
