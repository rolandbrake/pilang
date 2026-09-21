#ifndef PI_CLASS_H
#define PI_CLASS_H

#include "pi_object.h"

Object *new_class(const char *name, PiClass *super, table_t *members);
Object *new_instance(PiClass *_class);

bool class_getMember(PiClass *_class, const char *name, Value *out);
bool class_getMemberHash(PiClass *_class, const char *name, uint64_t hash, Value *out);
bool class_getFieldSlot(PiClass *_class, const char *name, uint16_t *slot);
bool class_getFieldSlotHash(PiClass *_class, const char *name, uint64_t hash, uint16_t *slot);

void class_setMember(PiClass *_class, const char *name, Value value);
bool class_deleteMember(PiClass *_class, const char *name);
uint64_t class_mutationVersion(void);

bool instance_getMember(PiInstance *instance, const char *name, Value *out);
bool instance_getMemberHash(PiInstance *instance, const char *name, uint64_t hash, Value *out);

void instance_setMember(PiInstance *instance, const char *name, Value value);

#endif // PI_CLASS_H
