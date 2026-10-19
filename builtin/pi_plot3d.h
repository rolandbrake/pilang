#ifndef PI_PLOT3D_H
#define PI_PLOT3D_H

#include "../src/pi_vm.h"
#include "../src/pi_value.h"

Value pt3d_surface(VM *vm, int argc, Value *argv);
Value pt3d_mesh(VM *vm, int argc, Value *argv);
Value pt3d_wireframe(VM *vm, int argc, Value *argv);
Value pt3d_scatter(VM *vm, int argc, Value *argv);

Value pt3d_chart(VM *vm, int argc, Value *argv);

Value pt3d_show(VM *vm, int argc, Value *argv);
void pt3d_RedrawContext(PiContext *ctx);

Value pt3d_title(VM *vm, int argc, Value *argv);
Value pt3d_xlabel(VM *vm, int argc, Value *argv);
Value pt3d_ylabel(VM *vm, int argc, Value *argv);
Value pt3d_zlabel(VM *vm, int argc, Value *argv);

Value pt3d_grid(VM *vm, int argc, Value *argv);

Value pt3d_view(VM *vm, int argc, Value *argv);
Value pt3d_subplot(VM *vm, int argc, Value *argv);

#endif // PI_PLOT3D_H
