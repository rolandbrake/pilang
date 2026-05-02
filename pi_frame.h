#ifndef PI_FRAME_H
#define PI_FRAME_H

#include "pi_list.h"
#include "pi_table.h"

// Forward declare Function to avoid circular include
typedef struct Function Function;

typedef struct
{
    int pc; // program counter
    int sp; // stack pointer
    int bp; // base pointer
    int ip; // instruction pointer

    list_t *code; // list of instructions
    list_t *constants; // constants table for the frame
    list_t *names; // names table for the frame
    table_t *instrs; // instruction metadata for the frame
    table_t *globals; // global environment for the frame

    int iters_top; // to track the state of iterators stack

    Function *function;
} Frame;

Frame *create_frame(int pc, int sp, int bp, list_t *code, list_t *constants, list_t *names, table_t *instrs, int iters_top, int ip, Function *fn);
void free_frame(Frame *frame);

#endif
