#ifndef PI_PLOT_H
#define PI_PLOT_H

#include "../pi_vm.h"
#include "../pi_value.h"
#include "../pi_object.h"

typedef struct
{
    SDL_Renderer *r;
    PiChart *chart;
    int W, H, margin, ymargin;
    int col; /* palette colour for this series */
} DrawContext;

Value pt_chart(vm_t *vm, int argc, Value *argv);

Value pt_func(vm_t *vm, int argc, Value *argv);
// returning Chart object
Value pt_scatter(vm_t *vm, int argc, Value *argv);
Value pt_bar(vm_t *vm, int argc, Value *argv);
Value pt_line(vm_t *vm, int argc, Value *argv);
Value pt_hist(vm_t *vm, int argc, Value *argv);
Value pt_step(vm_t *vm, int argc, Value *argv);
Value pt_heatmap(vm_t *vm, int argc, Value *argv);
Value pt_contour(vm_t *vm, int argc, Value *argv);
Value pt_quiver(vm_t *vm, int argc, Value *argv);
Value pt_streamplot(vm_t *vm, int argc, Value *argv);

// Chart functions
Value pt_show(vm_t *vm, int argc, Value *argv);
Value pt_title(vm_t *vm, int argc, Value *argv);
Value pt_xlabel(vm_t *vm, int argc, Value *argv);
Value pt_ylabel(vm_t *vm, int argc, Value *argv);
Value pt_tick(vm_t *vm, int argc, Value *argv);
Value pt_grid(vm_t *vm, int argc, Value *argv);
Value pt_axes(vm_t *vm, int argc, Value *argv);
Value pt_legend(vm_t *vm, int argc, Value *argv);
Value pt_subplot(vm_t *vm, int argc, Value *argv);

#endif // PI_PLOT_H
