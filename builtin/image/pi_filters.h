#ifndef PI_IMAGE_FILTERS_H
#define PI_IMAGE_FILTERS_H

#include "../pi_builtin.h"
#include "../../src/pi_value.h"



Value im_invert(VM *vm, int argc, Value *argv);
Value im_brightness(VM *vm, int argc, Value *argv);
Value im_contrast(VM *vm, int argc, Value *argv);
Value im_blur(VM *vm, int argc, Value *argv);
Value im_sharpen(VM *vm, int argc, Value *argv);
Value im_sobel(VM *vm, int argc, Value *argv);
Value im_threshold(VM *vm, int argc, Value *argv);
Value im_canny(VM *vm, int argc, Value *argv);
Value im_filter(VM *vm, int argc, Value *argv);
Value im_kernel(VM *vm, int argc, Value *argv);
Value im_boxKernel(VM *vm, int argc, Value *argv);

extern BuiltinModule module_imageFilters;


#endif // PI_IMAGE_FILTERS_H
