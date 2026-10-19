
#ifndef PI_IMAGE_H
#define PI_IMAGE_H

#include "../../src/pi_value.h"

Value im_load(VM *vm, int argc, Value *argv);
Value im_save(VM *vm, int argc, Value *argv);
Value im_width(VM *vm, int argc, Value *argv);
Value im_height(VM *vm, int argc, Value *argv);
Value im_channels(VM *vm, int argc, Value *argv);
Value im_overlaps(VM *vm, int argc, Value *argv);
Value im_resize(VM *vm, int argc, Value *argv);
Value im_crop(VM *vm, int argc, Value *argv);
Value im_flip(VM *vm, int argc, Value *argv);
Value im_rgb2gray(VM *vm, int argc, Value *argv);
Value im_gray2rgb(VM *vm, int argc, Value *argv);
Value im_gray2rgba(VM *vm, int argc, Value *argv);
Value im_show(VM *vm, int argc, Value *argv);
Value im_img2tensor(VM *vm, int argc, Value *argv);
Value im_tensor2img(VM *vm, int argc, Value *argv);

#endif // PI_IMAGE_H

