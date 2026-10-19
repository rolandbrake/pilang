#ifndef PI_PLOT_H
#define PI_PLOT_H

#include "../src/pi_vm.h"
#include "../src/pi_value.h"
#include "../src/pi_object.h"

typedef struct
{
    SDL_Renderer *r;
    PiChart *chart;
    int W, H, margin, ymargin;
    int col; /* palette colour for this series */
} DrawContext;

Value pt_chart(VM *vm, int argc, Value *argv);

Value pt_func(VM *vm, int argc, Value *argv);
// returning Chart object
Value pt_scatter(VM *vm, int argc, Value *argv);
Value pt_bar(VM *vm, int argc, Value *argv);
Value pt_line(VM *vm, int argc, Value *argv);
Value pt_hist(VM *vm, int argc, Value *argv);
Value pt_step(VM *vm, int argc, Value *argv);
Value pt_imshow(VM *vm, int argc, Value *argv);
Value pt_heatmap(VM *vm, int argc, Value *argv);
Value pt_contour(VM *vm, int argc, Value *argv);
Value pt_quiver(VM *vm, int argc, Value *argv);
Value pt_streamplot(VM *vm, int argc, Value *argv);

// Chart functions
Value pt_show(VM *vm, int argc, Value *argv);
Value pt_title(VM *vm, int argc, Value *argv);
Value pt_xlabel(VM *vm, int argc, Value *argv);
Value pt_ylabel(VM *vm, int argc, Value *argv);
Value pt_tick(VM *vm, int argc, Value *argv);
Value pt_grid(VM *vm, int argc, Value *argv);
Value pt_axes(VM *vm, int argc, Value *argv);
Value pt_legend(VM *vm, int argc, Value *argv);
Value pt_subplot(VM *vm, int argc, Value *argv);

#endif // PI_PLOT_H
