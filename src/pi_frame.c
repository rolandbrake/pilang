#include "pi_frame.h"

Frame *create_frame(int pc, int sp, int bp, List *code, List *constants, List *names, HT_Table *instrs, int iters_top, int ip, Function *fn)
{
    Frame *frame = (Frame *)malloc(sizeof(Frame));

    frame->code = code;
    frame->constants = constants;
    frame->names = names;
    frame->instrs = instrs;

    frame->pc = pc;
    frame->bp = bp;
    frame->sp = sp;
    frame->ip = ip;

    // Iterator stack position is preserved across function calls.
    frame->iters_top = iters_top;
    frame->global_cache = NULL;

    frame->function = fn;
    frame->is_recursive = FALSE;

    return frame;
}

void free_frame(Frame *frame)
{
    free(frame);
}
