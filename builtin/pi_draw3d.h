#ifndef PI_DRAW3D_H
#define PI_DRAW3D_H

#include "../src/pi_vm.h"

Value d3_canvas(VM *vm, int argc, Value *argv);
Value d3_run(VM *vm, int argc, Value *argv);
Value d3_clear(VM *vm, int argc, Value *argv);
Value d3_mesh(VM *vm, int argc, Value *argv);
Value d3_draw(VM *vm, int argc, Value *argv);
Value d3_animate(VM *vm, int argc, Value *argv);
Value d3_close(VM *vm, int argc, Value *argv);

void d3_cleanupContext(PiContext *ctx);

#endif
