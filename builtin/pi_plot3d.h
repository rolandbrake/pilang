#ifndef PI_PLOT3D_H
#define PI_PLOT3D_H

#include "../pi_vm.h"
#include "../pi_value.h"

Value pt3d_surface(vm_t *vm, int argc, Value *argv);
Value pt3d_mesh(vm_t *vm, int argc, Value *argv);
Value pt3d_wireframe(vm_t *vm, int argc, Value *argv);
Value pt3d_scatter(vm_t *vm, int argc, Value *argv);

Value pt3d_chart(vm_t *vm, int argc, Value *argv);

Value pt3d_show(vm_t *vm, int argc, Value *argv);
void pt3d_redraw_context(PiContext *ctx);

Value pt3d_title(vm_t *vm, int argc, Value *argv);
Value pt3d_xlabel(vm_t *vm, int argc, Value *argv);
Value pt3d_ylabel(vm_t *vm, int argc, Value *argv);
Value pt3d_zlabel(vm_t *vm, int argc, Value *argv);

Value pt3d_grid(vm_t *vm, int argc, Value *argv);

Value pt3d_view(vm_t *vm, int argc, Value *argv);
Value pt3d_subplot(vm_t *vm, int argc, Value *argv);

#endif // PI_PLOT3D_H
