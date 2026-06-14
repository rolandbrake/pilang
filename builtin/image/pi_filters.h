#ifndef PI_IMAGE_FILTERS_H
#define PI_IMAGE_FILTERS_H

#include "../pi_builtin.h"
#include "../../pi_value.h"



Value im_invert(vm_t *vm, int argc, Value *argv);
Value im_brightness(vm_t *vm, int argc, Value *argv);
Value im_contrast(vm_t *vm, int argc, Value *argv);
Value im_blur(vm_t *vm, int argc, Value *argv);
Value im_sharpen(vm_t *vm, int argc, Value *argv);
Value im_sobel(vm_t *vm, int argc, Value *argv);
Value im_threshold(vm_t *vm, int argc, Value *argv);
Value im_canny(vm_t *vm, int argc, Value *argv);
Value im_filter(vm_t *vm, int argc, Value *argv);
Value im_kernel(vm_t *vm, int argc, Value *argv);
Value im_boxKernel(vm_t *vm, int argc, Value *argv);

extern BuiltinModule module_imageFilters;


#endif // PI_IMAGE_FILTERS_H
