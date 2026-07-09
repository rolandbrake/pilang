#ifndef PI_CLASS_H
#define PI_CLASS_H

#include "pi_object.h"

Object *new_class(const char *name, PiClass *super);
Object *new_instance(PiClass *_class);

void class_setMethod(PiClass *_class, const char *name, Value method);
bool class_getMethod(PiClass *_class, const char *name, Value *out);

void class_setStatic(PiClass *_class, const char *name, Value value);
bool class_getStatic(PiClass *_class, const char *name, Value *out);

int class_getSlot(PiClass *_class, const char *name);
int class_ensureSlot(PiClass *_class, const char *name);

bool instance_getSlot(PiInstance *instance, const char *name, Value *out);
bool instance_setSlot(PiInstance *instance, const char *name, Value value);

bool instance_getField(PiInstance *instance, const char *name, Value *out);
void instance_setField(PiInstance *instance, const char *name, Value value);
bool instance_deleteField(PiInstance *instance, const char *name);

void mark_class(PiClass *_class);
void mark_instance(PiInstance *instance);

void free_class(PiClass *_class);
void free_instance(PiInstance *instance);

#endif
