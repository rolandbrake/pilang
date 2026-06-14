#ifndef PI_IMAGE_COLOR_H
#define PI_IMAGE_COLOR_H

#include "../pi_builtin.h"
#include "../../pi_value.h"

Value im_rgb2gray(vm_t *vm, int argc, Value *argv);
Value im_gray2rgb(vm_t *vm, int argc, Value *argv);
Value im_gray2rgba(vm_t *vm, int argc, Value *argv);
Value im_rgb2hsv(vm_t *vm, int argc, Value *argv);
Value im_hsv2rgb(vm_t *vm, int argc, Value *argv);



#endif // PI_IMAGE_COLOR_H
