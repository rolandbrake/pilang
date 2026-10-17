#ifndef PI_LOON_BUILTIN_H
#define PI_LOON_BUILTIN_H

#include <stdbool.h>
#include <stddef.h>

#include "../pi_vm.h"

typedef enum
{
    LOON_NULL,
    LOON_BOOL,
    LOON_NUMBER,
    LOON_STRING,
    LOON_ARRAY,
    LOON_OBJECT,
} LoonType;

typedef struct LoonValue LoonValue;
typedef struct
{
    char *key;
    LoonValue *value;
} LoonMember;

struct LoonValue
{
    LoonType type;
    union
    {
        bool boolean;
        double number;
        char *string;
        struct
        {
            LoonValue **items;
            size_t count;
        } array;
        struct
        {
            LoonMember *members;
            size_t count;
        } object;
    } as;
};

typedef struct
{
    size_t line;
    size_t column;
    char message[160];
} LoonError;

LoonValue *loon_parse(const char *source, LoonError *error);
void loon_free(LoonValue *value);
const LoonValue *loon_objectGet(const LoonValue *value, const char *key);

Value loon_parseValue(VM *vm, int argc, Value *argv);

#endif
