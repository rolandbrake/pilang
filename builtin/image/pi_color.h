#ifndef PI_IMAGE_COLOR_H
#define PI_IMAGE_COLOR_H

#include "../pi_builtin.h"
#include "../../src/pi_value.h"

Value im_rgb2gray(VM *vm, int argc, Value *argv);
Value im_gray2rgb(VM *vm, int argc, Value *argv);
Value im_gray2rgba(VM *vm, int argc, Value *argv);
Value im_rgb2hsv(VM *vm, int argc, Value *argv);
Value im_hsv2rgb(VM *vm, int argc, Value *argv);



#endif // PI_IMAGE_COLOR_H
