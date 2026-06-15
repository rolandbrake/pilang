
#ifndef PI_IMAGE_H
#define PI_IMAGE_H

#include "../../pi_value.h"

Value im_load(vm_t *vm, int argc, Value *argv);
Value im_save(vm_t *vm, int argc, Value *argv);
Value im_width(vm_t *vm, int argc, Value *argv);
Value im_height(vm_t *vm, int argc, Value *argv);
Value im_channels(vm_t *vm, int argc, Value *argv);
Value im_resize(vm_t *vm, int argc, Value *argv);
Value im_crop(vm_t *vm, int argc, Value *argv);
Value im_flip(vm_t *vm, int argc, Value *argv);
Value im_rgb2gray(vm_t *vm, int argc, Value *argv);
Value im_gray2rgb(vm_t *vm, int argc, Value *argv);
Value im_gray2rgba(vm_t *vm, int argc, Value *argv);
Value im_show(vm_t *vm, int argc, Value *argv);
Value im_img2tensor(vm_t *vm, int argc, Value *argv);
Value im_tensor2img(vm_t *vm, int argc, Value *argv);

#endif // PI_IMAGE_H

